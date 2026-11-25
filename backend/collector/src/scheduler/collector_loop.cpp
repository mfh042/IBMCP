#include "collector_loop.h"

#include <chrono>
#include <random>
#include <unordered_map>
#include <utility>
#include <vector>

#include "drivers/driver.h"
#include "drivers/driver_factory.h"
#include "scheduler/sim_value.h"

namespace ibmcp {

// 采集循环的不可变快照：plan + 已构建的驱动集合 + 降级标志。
// 必须定义在 ibmcp 命名空间（非匿名），与 collector_loop.h 中的前向声明
// ibmcp::LoopSnapshot 是同一类型；若放进匿名命名空间会变成 ibmcp::<unnamed>::LoopSnapshot，
// 与头文件成员 snap_ 的类型不一致，导致赋值/模板实参失效。
// reload 时整体替换；正在执行的 IO 任务持有 shared_ptr 拷贝，旧快照延迟释放，无 UAF。
struct LoopSnapshot {
    std::shared_ptr<const Plan> plan;
    std::unordered_map<std::string, std::shared_ptr<IDriver>> drivers;
    std::unordered_map<std::string, bool> degraded;
    // 设备级 IO 串行锁：read_batch 与 write 共用，避免并发访问同一驱动连接上下文。
    // 锁的生命周期跟随 snapshot，reload 时旧锁随旧快照延迟释放，正在执行的 IO 任务无 UAF。
    std::unordered_map<std::string, std::shared_ptr<std::mutex>> io_locks;
};

namespace {

int64_t now_epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

CollectorLoop::CollectorLoop(DataStore& store, EventEmitter& emitter,
                             std::function<std::shared_ptr<const Plan>()> plan_getter,
                             DriverFactory driver_factory, size_t io_threads)
    : store_(store),
      emitter_(emitter),
      plan_getter_(std::move(plan_getter)),
      driver_factory_(std::move(driver_factory)),
      io_pool_(io_threads) {}

CollectorLoop::~CollectorLoop() { stop(); }

void CollectorLoop::start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) return;
    io_pool_.start();
    wheel_.start();
    mgmt_timer_ = wheel_.add_periodic(mgmt_interval_ms_.load(), [this] { check_reload(); });
    int rc = reconnect_interval_ms_.load();
    if (rc > 0) {
        reconnect_timer_ = wheel_.add_periodic(rc, [this] { try_reconnect(); });
    }
    check_reload(); // 同步加载首次计划，避免空等一个管理周期
}

void CollectorLoop::stop() {
    bool expected = true;
    if (!started_.compare_exchange_strong(expected, false)) return;
    wheel_.stop();   // 先停时间轮：不再产生新的读任务 / 重连任务
    io_pool_.stop(); // 排空已提交的读任务后退出
    reconnect_timer_ = 0;
    mgmt_timer_ = 0;
    std::shared_ptr<const LoopSnapshot> snap;
    {
        std::lock_guard<std::mutex> lk(snap_mtx_);
        snap = snap_;
        snap_.reset();
    }
    if (snap) {
        for (auto& kv : snap->drivers) {
            if (kv.second) {
                try {
                    kv.second->disconnect();
                } catch (...) {
                }
            }
        }
    }
}

void CollectorLoop::set_interval_ms(int ms) {
    int v = ms > 0 ? ms : 200;
    mgmt_interval_ms_.store(v);
    if (started_.load()) {
        wheel_.cancel(mgmt_timer_);
        mgmt_timer_ = wheel_.add_periodic(v, [this] { check_reload(); });
    }
}

void CollectorLoop::set_reconnect_interval_ms(int ms) {
    int v = ms > 0 ? ms : 0;
    reconnect_interval_ms_.store(v);
    if (started_.load()) {
        if (reconnect_timer_ != 0) {
            wheel_.cancel(reconnect_timer_);
            reconnect_timer_ = 0;
        }
        if (v > 0) {
            reconnect_timer_ = wheel_.add_periodic(v, [this] { try_reconnect(); });
        }
    }
}

void CollectorLoop::check_reload() {
    std::lock_guard<std::mutex> lk(reload_mtx_);
    auto plan = plan_getter_ ? plan_getter_() : nullptr;
    int new_rev = plan ? plan->revision : -1;
    int cur_rev = -1;
    {
        std::lock_guard<std::mutex> slk(snap_mtx_);
        if (snap_ && snap_->plan) cur_rev = snap_->plan->revision;
    }
    if (new_rev == cur_rev) return; // 无变化（含「当前无 plan 且仍无 plan」）
    reload(plan);
}

void CollectorLoop::reload(std::shared_ptr<const Plan> plan) {
    // 取消旧设备定时器（提交中的读任务靠 snap 拷贝继续完成）
    for (auto id : device_timers_) wheel_.cancel(id);
    device_timers_.clear();

    auto snap = std::make_shared<LoopSnapshot>();
    snap->plan = plan;

    if (!plan) {
        std::lock_guard<std::mutex> lk(snap_mtx_);
        snap_ = snap;
        return;
    }

    for (const auto& dev : plan->devices) {
        std::shared_ptr<IDriver> drv;
        bool degraded = true;
        std::string reason = "driver_unavailable";
        auto factory = driver_factory_ ? driver_factory_ : create_driver;
        try {
            auto raw = factory(normalize_protocol(dev.protocol));
            if (raw) {
                if (raw->connect(dev.connection, dev.connect_timeout_ms, dev.read_timeout_ms)) {
                    degraded = false;
                    reason = "connected";
                } else {
                    reason = "connect_failed";
                }
                drv.reset(raw.release());
            }
        } catch (...) {
            drv.reset();
            degraded = true;
            reason = "connect_failed";
        }
        snap->drivers[dev.device_key] = drv;
        snap->degraded[dev.device_key] = degraded;
        snap->io_locks[dev.device_key] = std::make_shared<std::mutex>();

        // 循环线程发出权威 device_status：真实连接结果 + 是否降级回退模拟值，
        // 与 stub 侧事件契约对齐（status 1/0、reason、source、ts_ms）。
        emitter_.emit({
            {"type", "device_status"},
            {"device_key", dev.device_key},
            {"status", degraded ? "0" : "1"},
            {"reason", reason},
            {"source", degraded ? "sim" : "real"},
            {"ts_ms", std::to_string(now_epoch_ms())},
        });

        // 设备调度周期 = 设备内点位 min(cycle_ms)，下限 50ms。
        int cycle = 0;
        for (const auto& pt : dev.points) {
            if (cycle == 0 || pt.cycle_ms < cycle) cycle = pt.cycle_ms;
        }
        if (cycle < 50) cycle = 50;

        std::string dk = dev.device_key;
        auto tid = wheel_.add_periodic(cycle, [this, dk] {
            io_pool_.submit([this, dk] { read_device(dk); });
        });
        if (tid != 0) device_timers_.push_back(tid);
    }

    std::lock_guard<std::mutex> lk(snap_mtx_);
    snap_ = snap;
}

void CollectorLoop::read_device(const std::string& device_key) {
    std::shared_ptr<const LoopSnapshot> snap;
    {
        std::lock_guard<std::mutex> lk(snap_mtx_);
        snap = snap_;
    }
    if (!snap || !snap->plan) return;

    const Device* dev = nullptr;
    for (const auto& d : snap->plan->devices) {
        if (d.device_key == device_key) {
            dev = &d;
            break;
        }
    }
    if (!dev) return;

    const int64_t now_ms = now_epoch_ms();

    auto drv_it = snap->drivers.find(device_key);
    bool degraded = true;
    if (drv_it != snap->drivers.end() && drv_it->second) {
        auto dg_it = snap->degraded.find(device_key);
        if (dg_it != snap->degraded.end()) degraded = dg_it->second;
    }

    if (degraded) {
        // 驱动不可用 / 未连接：回退模拟值，行为对齐 stub，不退化。
        thread_local std::mt19937 rng(std::random_device{}());
        thread_local std::uniform_real_distribution<double> u01(0.0, 1.0);
        for (const auto& pt : dev->points) {
            Sample s;
            s.device_key = device_key;
            s.point_id = pt.point_id;
            s.value = make_sim_value(pt, now_ms, rng);
            s.quality = u01(rng) < 0.01 ? "Uncertain" : "Good";
            s.ts_ms = now_ms;
            store_.push(s);
        }
        return;
    }

    // 已连接：真实读取。读失败如实写 Bad 质量（不回退模拟值，真实反映故障）。
    // 与 write_device 共用设备级锁串行，避免并发访问同一驱动连接上下文。
    std::shared_ptr<std::mutex> dev_lock;
    auto lit = snap->io_locks.find(device_key);
    if (lit != snap->io_locks.end()) dev_lock = lit->second;
    std::unique_lock<std::mutex> io_guard;
    if (dev_lock) io_guard = std::unique_lock<std::mutex>(*dev_lock);
    std::vector<ReadResult> results;
    try {
        results = drv_it->second->read_batch(dev->points);
    } catch (...) {
        results.clear();
    }
    std::unordered_map<int, ReadResult> by_id;
    for (auto& r : results) by_id.emplace(r.point_id, std::move(r));

    int good_count = 0;
    for (const auto& pt : dev->points) {
        Sample s;
        s.device_key = device_key;
        s.point_id = pt.point_id;
        s.ts_ms = now_ms;
        auto rit = by_id.find(pt.point_id);
        if (rit == by_id.end() || rit->second.quality == "Bad" || !rit->second.error.empty()) {
            s.quality = "Bad";
            s.value.clear();
        } else {
            s.quality = rit->second.quality.empty() ? "Good" : rit->second.quality;
            s.value = rit->second.value;
            ++good_count;
        }
        store_.push(s);
    }
    // 整设备本次采集无任何 Good 点位：判定设备掉线，翻转到降级（后续周期回退模拟值），
    // 并发出 device_status；由重连定时器周期性尝试恢复。仅状态变化时发事件，不刷屏。
    if (!dev->points.empty() && good_count == 0) {
        mark_degraded(device_key, "read_failed");
    }
}

bool CollectorLoop::write_device(const std::string& device_key, int point_id,
                                 const std::string& value) {
    std::shared_ptr<const LoopSnapshot> snap;
    {
        std::lock_guard<std::mutex> lk(snap_mtx_);
        snap = snap_;
    }
    if (!snap || !snap->plan) return false;

    const Point* target = nullptr;
    for (const auto& d : snap->plan->devices) {
        if (d.device_key != device_key) continue;
        for (const auto& p : d.points) {
            if (p.point_id == point_id) {
                target = &p;
                break;
            }
        }
        break;
    }
    if (!target) return false;

    auto drv_it = snap->drivers.find(device_key);
    if (drv_it == snap->drivers.end() || !drv_it->second) return false;

    // 设备级串行锁（与 read_device 真实读取共用），保证同设备 read/write 不并发。
    std::shared_ptr<std::mutex> dev_lock;
    auto lit = snap->io_locks.find(device_key);
    if (lit != snap->io_locks.end()) dev_lock = lit->second;
    std::unique_lock<std::mutex> io_guard;
    if (dev_lock) io_guard = std::unique_lock<std::mutex>(*dev_lock);

    try {
        return drv_it->second->write(*target, value);
    } catch (...) {
        return false;
    }
}

std::pair<int, int> CollectorLoop::device_counts() const {
    std::lock_guard<std::mutex> lk(snap_mtx_);
    if (!snap_ || !snap_->plan) return {0, 0};
    int total = static_cast<int>(snap_->plan->devices.size());
    int degraded = 0;
    for (const auto& kv : snap_->degraded) {
        if (kv.second) ++degraded;
    }
    return {total, degraded};
}

void CollectorLoop::mark_degraded(const std::string& device_key, const std::string& reason) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lk(snap_mtx_);
        if (!snap_) return;
        auto it = snap_->degraded.find(device_key);
        if (it == snap_->degraded.end() || it->second) return; // 未知设备或已降级
        auto copy = std::make_shared<LoopSnapshot>(*snap_);
        copy->degraded[device_key] = true;
        snap_ = copy;
        changed = true;
    }
    if (changed) {
        emitter_.emit({
            {"type", "device_status"},
            {"device_key", device_key},
            {"status", "0"},
            {"reason", reason},
            {"source", "sim"},
            {"ts_ms", std::to_string(now_epoch_ms())},
        });
    }
}

void CollectorLoop::mark_reconnected(const std::string& device_key,
                                     std::shared_ptr<IDriver> driver) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lk(snap_mtx_);
        if (!snap_) return;
        auto it = snap_->degraded.find(device_key);
        if (it == snap_->degraded.end() || !it->second) return; // 未降级
        auto copy = std::make_shared<LoopSnapshot>(*snap_);
        copy->drivers[device_key] = std::move(driver);
        copy->degraded[device_key] = false;
        snap_ = copy;
        changed = true;
    }
    if (changed) {
        emitter_.emit({
            {"type", "device_status"},
            {"device_key", device_key},
            {"status", "1"},
            {"reason", "reconnected"},
            {"source", "real"},
            {"ts_ms", std::to_string(now_epoch_ms())},
        });
    }
}

void CollectorLoop::try_reconnect() {
    std::shared_ptr<const LoopSnapshot> snap;
    {
        std::lock_guard<std::mutex> lk(snap_mtx_);
        snap = snap_;
    }
    if (!snap || !snap->plan) return;
    auto factory = driver_factory_ ? driver_factory_ : create_driver;
    for (const auto& dev : snap->plan->devices) {
        auto dit = snap->degraded.find(dev.device_key);
        if (dit == snap->degraded.end() || !dit->second) continue; // 只重连降级设备
        std::shared_ptr<IDriver> drv;
        bool ok = false;
        try {
            auto raw = factory(normalize_protocol(dev.protocol));
            if (raw) {
                if (raw->connect(dev.connection, dev.connect_timeout_ms, dev.read_timeout_ms)) {
                    ok = true;
                }
                drv.reset(raw.release());
            }
        } catch (...) {
            drv.reset();
        }
        if (ok && drv) {
            mark_reconnected(dev.device_key, std::move(drv));
        }
        // 重连失败不发事件：维持降级状态，等待下一轮周期，避免周期刷屏。
    }
}

} // namespace ibmcp