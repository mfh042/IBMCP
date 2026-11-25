#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/event_emitter.h"
#include "plan/plan.h"
#include "scheduler/io_pool.h"
#include "scheduler/timewheel.h"
#include "store/data_store.h"

namespace ibmcp {

struct IDriver; // 前向声明：DriverFactory 签名里只需类型名，完整定义在 .cpp include
struct LoopSnapshot; // 内部快照，定义在 .cpp

// 采集循环：时间轮按设备 cycle_ms 调度 → IO 线程池调用驱动 read_batch → 写入 DataStore。
// 驱动不可用 / 连接失败时回退到模拟值生成（对齐 stub 曲线，行为不退化）。
// 设计约束：
//   * IO 线程只写 DataStore，不调用 EventEmitter（非 Python 线程触发 Python 回调需 GIL，
//     交给 Python 线程的 API 调用点发出，避免 GIL 争用与回调重入）。
//   * plan 通过 plan_getter 快照获取；管理 tick 检测 revision 变化后原子替换内部快照，
//     旧驱动由 shared_ptr 延迟释放，正在执行的读任务无 UAF。
class CollectorLoop {
public:
    using DriverFactory = std::function<std::unique_ptr<IDriver>(const std::string&)>;

    CollectorLoop(DataStore& store, EventEmitter& emitter,
                  std::function<std::shared_ptr<const Plan>()> plan_getter,
                  DriverFactory driver_factory = nullptr,
                  size_t io_threads = 0);
    ~CollectorLoop();

    CollectorLoop(const CollectorLoop&) = delete;
    CollectorLoop& operator=(const CollectorLoop&) = delete;

    void start();
    void stop();
    // 管理 tick（plan 重载检测）周期，默认 200ms。仅影响热切换检测延迟，不影响点位采集周期。
    void set_interval_ms(int ms);

    // 降级设备的重连尝试周期，默认 5000ms；<=0 关闭重连。
    void set_reconnect_interval_ms(int ms);

    // IO 线程池实际工作线程数（start 后有效）。
    size_t io_pool_size() const { return io_pool_.size(); }

    // 当前计划中的设备总数，与其中处于降级（回退模拟值）状态的设备数。
    // 供 health / collector_stats 上报，让上层区分真实在线与模拟在线。
    std::pair<int, int> device_counts() const;

    // 同步执行单点控制写：与该设备的 read_batch 共用设备级锁串行执行，
    // 避免并发访问同一驱动连接上下文（libmodbus ctx 非线程安全）。
    // 驱动 write 的返回值即结果；驱动缺失 / 点位不存在 / 写失败 / 抛异常均返回 false（不抛异常）。
    bool write_device(const std::string& device_key, int point_id, const std::string& value);

private:
    void check_reload();
    void reload(std::shared_ptr<const Plan> plan);
    void read_device(const std::string& device_key);
    void try_reconnect();
    // 把设备标记为降级（copy-on-write 替换快照，避免与在读任务竞争）；仅在状态变化时发事件。
    void mark_degraded(const std::string& device_key, const std::string& reason);
    // 重连成功：替换驱动并清除降级标志；仅在状态变化时发事件。
    void mark_reconnected(const std::string& device_key, std::shared_ptr<IDriver> driver);

    DataStore& store_;
    EventEmitter& emitter_;
    std::function<std::shared_ptr<const Plan>()> plan_getter_;
    DriverFactory driver_factory_;
    TimeWheel wheel_;
    IoPool io_pool_;
    std::atomic<bool> started_{false};
    std::atomic<int> mgmt_interval_ms_{200};
    std::atomic<int> reconnect_interval_ms_{5000};
    TimeWheel::TimerId mgmt_timer_{0};
    TimeWheel::TimerId reconnect_timer_{0};
    std::vector<TimeWheel::TimerId> device_timers_; // 仅在 reload_mtx_ 保护下访问
    std::mutex reload_mtx_;
    mutable std::mutex snap_mtx_;
    std::shared_ptr<const LoopSnapshot> snap_;
};

} // namespace ibmcp