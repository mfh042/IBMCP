#include "collector/collector.h"
#include "collector/exports.h"
#include "common/event_emitter.h"
#include "drivers/driver_factory.h"
#include "plan/plan.h"
#include "scheduler/collector_loop.h"
#include "store/data_store.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ibmcp {

struct Collector::Impl {
    bool running = false;
    int current_revision = 0;
    // current_plan 用 shared_ptr：采集循环线程通过快照读取，与 apply_plan 的写入互不阻塞。
    std::shared_ptr<const Plan> current_plan;
    mutable std::mutex plan_mtx;
    DataStore store;
    EventEmitter emitter;
    std::unique_ptr<CollectorLoop> loop;
    std::atomic<int> snapshot_calls{0};
    std::chrono::steady_clock::time_point start_time;
};

Collector& Collector::instance() { static Collector inst; return inst; }
Collector::Collector() : impl_(std::make_unique<Impl>()) {}
Collector::~Collector() = default;

int Collector::init(const std::map<std::string, std::string>& cfg) {
    try {
        size_t io_threads = 0; // 0 = 用默认 max(1, min(hardware_concurrency(), 8))
        auto cit = cfg.find("io_threads");
        if (cit != cfg.end()) {
            try {
                int n = std::stoi(cit->second);
                if (n > 0) io_threads = static_cast<size_t>(n);
            } catch (...) {
            }
        }
        impl_->start_time = std::chrono::steady_clock::now();
        impl_->running = true;
        if (impl_->loop) impl_->loop->stop();
        impl_->loop = std::make_unique<CollectorLoop>(
            impl_->store, impl_->emitter,
            [this]() -> std::shared_ptr<const Plan> {
                std::lock_guard<std::mutex> lk(impl_->plan_mtx);
                return impl_->current_plan;
            },
            nullptr, io_threads);
        impl_->loop->start();
        return 0;
    } catch (const std::exception& e) {
        throw CollectorError(-1, "init", "", -1, e.what());
    }
}

std::map<std::string, std::string> Collector::health() const {
    std::shared_ptr<const Plan> plan;
    int revision;
    {
        std::lock_guard<std::mutex> lk(impl_->plan_mtx);
        plan = impl_->current_plan;
        revision = impl_->current_revision;
    }
    int device_count = 0;
    int point_count = 0;
    if (plan) {
        device_count = static_cast<int>(plan->devices.size());
        for (const auto& dev : plan->devices) {
            point_count += static_cast<int>(dev.points.size());
        }
    }
    int devices_degraded = 0;
    if (impl_->loop) {
        auto counts = impl_->loop->device_counts();
        devices_degraded = counts.second;
    }
    int devices_real = device_count - devices_degraded;
    if (devices_real < 0) devices_real = 0;
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - impl_->start_time).count();
    std::string protos;
    for (const auto& p : supported_protocols()) {
        if (!protos.empty()) protos += ",";
        protos += p;
    }
    return {
        {"abi", "1.0"},
        {"lib", "0.1.0"},
        {"protocols", protos},
        {"threads", std::to_string(impl_->loop ? impl_->loop->io_pool_size() : 0)},
        {"revision", std::to_string(revision)},
        {"device_count", std::to_string(device_count)},
        {"devices_real", std::to_string(devices_real)},
        {"devices_degraded", std::to_string(devices_degraded)},
        {"point_count", std::to_string(point_count)},
        {"queue_depth", std::to_string(impl_->store.size())},
        {"dropped", std::to_string(impl_->store.dropped())},
        {"uptime_sec", std::to_string(uptime)},
    };
}

std::vector<std::string> Collector::list_protocols() const {
    return supported_protocols();
}

std::map<std::string, std::map<std::string, int>> Collector::protocol_capabilities() const {
    std::map<std::string, std::map<std::string, int>> out;
    // 诚实报告：只有当前编译目标真正编译进来的驱动（受 IBMCP_HAS_* 宏控制）才具备能力。
    // 未编译的协议（缺依赖的 socket_tcp/udp/websocket，或本机缺失驱动库的协议）
    // compiled=0、supports_write=0、max_points=0，不再无条件宣称支持全部四协议。
    std::set<std::string> avail;
    for (const auto& a : available_driver_protocols()) avail.insert(a);
    for (const auto& p : list_protocols()) {
        bool compiled = avail.count(normalize_protocol(p)) > 0;
        out[p] = {
            {"compiled", compiled ? 1 : 0},
            {"supports_write", compiled ? 1 : 0},
            {"max_points_per_device", compiled ? 512 : 0},
        };
    }
    return out;
}

int Collector::apply_plan(const nlohmann::json& plan, int revision) {
    try {
        if (!impl_->running) {
            throw NotInitialized(4, "apply_plan", "", -1,
                                 "collector not initialized, call init() first");
        }
        {
            std::lock_guard<std::mutex> lk(impl_->plan_mtx);
            if (impl_->current_plan != nullptr && revision == impl_->current_revision) {
                throw PlanRevisionConflict(
                    3, "apply_plan", "", -1,
                    "revision " + std::to_string(revision) + " already applied with different plan");
            }
            if (revision < impl_->current_revision) {
                throw PlanRevisionConflict(
                    3, "apply_plan", "", -1,
                    "revision " + std::to_string(revision) + " is older than current " +
                        std::to_string(impl_->current_revision));
            }
        }

        Plan parsed = parse_plan(plan);
        validate_plan(parsed);
        auto new_plan = std::make_shared<const Plan>(std::move(parsed));
        {
            std::lock_guard<std::mutex> lk(impl_->plan_mtx);
            impl_->current_plan = new_plan;
            impl_->current_revision = revision;
        }

        // 事件在锁外发出（emit 内部有加锁，且回调可能重入 apply_plan，避免死锁）。
        impl_->emitter.emit({
            {"type", "plan_ack"},
            {"revision", std::to_string(revision)},
            {"device_count", std::to_string(new_plan->devices.size())},
        });
        // 注意：device_status 不在此同步乐观上报——那会谎称未连接设备为在线。
        // 采集循环在 reload() 中基于真实驱动连接结果权威发出 device_status（含 degraded 标志），
        // 整设备读失败翻转降级、重连恢复也由循环线程发事件，前端看到的是真实状态。
        return 0;
    } catch (const CollectorError&) {
        throw;
    } catch (const std::exception& e) {
        throw CollectorError(-1, "apply_plan", "", revision, e.what());
    }
}

void Collector::set_event_callback(
        std::function<void(const std::map<std::string, std::string>&)> cb) {
    try {
        impl_->emitter.set_callback(std::move(cb));
    } catch (const std::exception& e) {
        throw CollectorError(-1, "set_event_callback", "", -1, e.what());
    }
}

std::pair<int, std::vector<std::map<std::string, std::string>>>
Collector::snapshot_since(int since_seq) {
    try {
        if (!impl_->running) {
            throw NotInitialized(4, "snapshot_since", "", -1, "collector not initialized");
        }
        auto result = impl_->store.snapshot_since(since_seq);
        int calls = impl_->snapshot_calls.fetch_add(1) + 1;
        if (calls % 10 == 0) {
            int devices_total = 0;
            int devices_degraded = 0;
            if (impl_->loop) {
                auto counts = impl_->loop->device_counts();
                devices_total = counts.first;
                devices_degraded = counts.second;
            }
            int devices_online = devices_total - devices_degraded;
            if (devices_online < 0) devices_online = 0;
            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            impl_->emitter.emit({
                {"type", "collector_stats"},
                {"ts", std::to_string(now)},
                {"total_samples", std::to_string(impl_->store.max_seq())},
                {"queue_depth", std::to_string(impl_->store.size())},
                {"dropped", std::to_string(impl_->store.dropped())},
                {"devices_online", std::to_string(devices_online)},
                {"devices_degraded", std::to_string(devices_degraded)},
            });
        }
        return result;
    } catch (const CollectorError&) {
        throw;
    } catch (const std::exception& e) {
        throw CollectorError(-1, "snapshot_since", "", -1, e.what());
    }
}

int Collector::write_point(const std::string& device_key, int point_id,
                           const std::string& value) {
    try {
        if (!impl_->running) {
            throw NotInitialized(4, "write_point", device_key, point_id,
                                 "collector not initialized");
        }
        std::shared_ptr<const Plan> plan;
        {
            std::lock_guard<std::mutex> lk(impl_->plan_mtx);
            plan = impl_->current_plan;
        }
        if (!plan) {
            throw DeviceNotInPlan(6, "write_point", device_key, point_id,
                                  "no active plan, cannot write " + device_key + ":" +
                                      std::to_string(point_id));
        }
        bool found = false;
        for (const auto& dev : plan->devices) {
            if (dev.device_key != device_key) continue;
            for (const auto& pt : dev.points) {
                if (pt.point_id == point_id) { found = true; break; }
            }
            if (found) break;
        }
        if (!found) {
            throw DeviceNotInPlan(6, "write_point", device_key, point_id,
                                  "device=" + device_key + " point=" +
                                      std::to_string(point_id) + " not in active plan");
        }
        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> dist(1000000, 9999999);
        int cmd_id = dist(rng);
        // 真实控制写：在调用线程同步执行，与该设备的 read 共用设备级锁串行。
        // write_device 内部吞掉驱动异常，仅以 bool 反映成败，不在此抛异常。
        bool ok = impl_->loop ? impl_->loop->write_device(device_key, point_id, value) : false;
        auto finished_at = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        impl_->emitter.emit({
            {"type", "write_result"},
            {"command_id", std::to_string(cmd_id)},
            {"device_key", device_key},
            {"point_id", std::to_string(point_id)},
            {"success", ok ? "True" : "False"},
            {"status", ok ? "ok" : "error"},
            {"value", value},
            {"finished_at", std::to_string(finished_at)},
        });
        return cmd_id;
    } catch (const CollectorError&) {
        throw;
    } catch (const std::exception& e) {
        throw CollectorError(-1, "write_point", device_key, point_id, e.what());
    }
}

void Collector::shutdown() {
    try {
        std::shared_ptr<const Plan> plan;
        {
            std::lock_guard<std::mutex> lk(impl_->plan_mtx);
            plan = impl_->current_plan;
        }
        if (plan) {
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            for (const auto& dev : plan->devices) {
                impl_->emitter.emit({
                    {"type", "device_status"},
                    {"device_key", dev.device_key},
                    {"status", "0"},
                    {"reason", "collector shutdown"},
                    {"ts_ms", std::to_string(now_ms)},
                });
            }
        }
        if (impl_->loop) impl_->loop->stop();
        impl_->running = false;
        {
            std::lock_guard<std::mutex> lk(impl_->plan_mtx);
            impl_->current_plan = nullptr;
        }
    } catch (const std::exception& e) {
        throw CollectorError(-1, "shutdown", "", -1, e.what());
    }
}

} // namespace ibmcp