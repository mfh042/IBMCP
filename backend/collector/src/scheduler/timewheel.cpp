#include "timewheel.h"

namespace ibmcp {

TimeWheel::TimeWheel() = default;

TimeWheel::~TimeWheel() { stop(); }

void TimeWheel::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    stop_.store(false);
    thread_ = std::thread([this] { run(); });
}

void TimeWheel::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) return;
    stop_.store(true);
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

TimeWheel::TimerId TimeWheel::add_periodic(int interval_ms, std::function<void()> cb) {
    if (interval_ms <= 0 || !cb) return 0;
    auto entry = std::make_shared<Entry>();
    entry->id = next_id_.fetch_add(1);
    entry->interval_ms = interval_ms;
    entry->cb = std::move(cb);
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(interval_ms);
    {
        std::lock_guard<std::mutex> lk(mtx_);
        entries_.emplace(entry->id, entry);
        timers_.emplace(deadline, entry);
    }
    cv_.notify_one();
    return entry->id;
}

void TimeWheel::cancel(TimerId id) {
    if (id == 0) return;
    std::lock_guard<std::mutex> lk(mtx_);
    // 先标记失活：即使 entry 正在执行回调（此时不在 timers_ 中），run 执行完也会因
    // active==false 而不再重新入队，保证取消彻底。
    auto eit = entries_.find(id);
    if (eit != entries_.end()) {
        eit->second->active.store(false);
        entries_.erase(eit);
    }
    for (auto it = timers_.begin(); it != timers_.end();) {
        if (it->second->id == id) {
            it = timers_.erase(it);
        } else {
            ++it;
        }
    }
    cv_.notify_one();
}

void TimeWheel::run() {
    using clock = std::chrono::steady_clock;
    std::unique_lock<std::mutex> lk(mtx_);
    while (!stop_.load()) {
        if (timers_.empty()) {
            cv_.wait(lk, [this] { return stop_.load() || !timers_.empty(); });
            continue;
        }
        // 只缓存 deadline（值类型），绝不缓存迭代器：wait 期间 cancel/add 会修改 timers_，
        // 跨 wait 持有的迭代器会失效，唤醒后必须重新取 begin()（旧实现因此崩溃）。
        clock::time_point deadline = timers_.begin()->first;
        cv_.wait_until(lk, deadline, [this] { return stop_.load(); });
        if (stop_.load()) break;
        // 处理所有已到期项：可能多个同时到期，或执行回调期间又有到期的。
        // 每轮重新取 begin()，不使用跨 erase 的旧迭代器。
        while (!timers_.empty() && timers_.begin()->first <= clock::now()) {
            auto it = timers_.begin();
            auto entry = it->second;
            clock::time_point fired_deadline = it->first;
            timers_.erase(it);
            if (!entry->active.load()) continue; // 已被 cancel，跳过且不重入队
            lk.unlock();
            entry->cb();
            lk.lock();
            // 回调执行期间可能被 cancel（active 被置 false），则不再重新入队。
            if (!stop_.load() && entry->active.load()) {
                timers_.emplace(fired_deadline +
                                    std::chrono::milliseconds(entry->interval_ms),
                                entry);
            }
            if (stop_.load()) break;
        }
    }
}

} // namespace ibmcp