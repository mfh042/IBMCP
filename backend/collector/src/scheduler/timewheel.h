#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <unordered_map>

namespace ibmcp {

// 单线程时间轮：注册周期性定时器，到期在时间轮线程触发回调。
// 回调应快速（只负责把任务提交到 IoPool），不要在回调里做阻塞 IO。
// 线程安全：start/stop/add_periodic/cancel 可从任意线程调用。
class TimeWheel {
public:
    using TimerId = uint64_t;

    TimeWheel();
    ~TimeWheel();

    TimeWheel(const TimeWheel&) = delete;
    TimeWheel& operator=(const TimeWheel&) = delete;

    void start();
    void stop();

    // 注册周期性定时器。interval_ms<=0 或 cb 为空时不注册，返回 0。
    // 首次到期在 interval_ms 之后，之后按 interval_ms 周期触发，直到 cancel 或 stop。
    TimerId add_periodic(int interval_ms, std::function<void()> cb);

    // 取消定时器。幂等。
    void cancel(TimerId id);

private:
    struct Entry {
        TimerId id = 0;
        int interval_ms = 0;
        std::function<void()> cb;
        std::atomic<bool> active{true};
    };

    void run();

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::atomic<TimerId> next_id_{1};
    std::thread thread_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::multimap<std::chrono::steady_clock::time_point, std::shared_ptr<Entry>> timers_;
    // 全部存活定时器（含正在执行回调、暂时不在 timers_ 中的）。cancel 通过它总能找到
    // entry 并置 active=false，避免「run 取出执行期间 cancel 不到」导致的取消不彻底。
    std::unordered_map<TimerId, std::shared_ptr<Entry>> entries_;
};

} // namespace ibmcp