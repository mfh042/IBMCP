#include "collector/collector.h"
#include "collector/exports.h"
#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace ibmcp {

struct Collector::Impl {
    bool running = false;
    int current_revision = 0;
};

Collector& Collector::instance() { static Collector inst; return inst; }
Collector::Collector() : impl_(std::make_unique<Impl>()) {}
Collector::~Collector() = default;

int Collector::init(const std::map<std::string, std::string>& cfg) {
    try { impl_->running = true; return 0; }
    catch (const std::exception& e) { throw CollectorError(-1, "init", "", -1, e.what()); }
}

std::map<std::string, std::string> Collector::health() const {
    return {{"abi","1.0"},{"lib","0.1.0"},{"threads","0"},{"revision",std::to_string(impl_->current_revision)}};
}

std::vector<std::string> Collector::list_protocols() const {
    return {"modbus_tcp","modbus_rtu","opc_ua","mqtt","socket_tcp","socket_udp","websocket"};
}

std::map<std::string, std::map<std::string, int>> Collector::protocol_capabilities() const {
    std::map<std::string, std::map<std::string, int>> out;
    for (auto& p : list_protocols()) out[p] = {{"supports_write",1},{"max_points_per_device",512}};
    return out;
}

int Collector::apply_plan(const py::dict& plan, int revision) {
    try {
        py::gil_scoped_release release_gil;
        if (revision <= impl_->current_revision) return 0;
        
        // TODO: 补充结构校验逻辑 (协议白名单、scale=0、address 为空等)
        // 校验失败时抛出对应异常码，例如：
        // if (!plan.contains("devices")) throw CollectorError(4001, "apply_plan", "", -1, "Missing devices");
        
        impl_->current_revision = revision;
        return 0;
    } catch (const CollectorError&) {
        throw; 
    } catch (const std::exception& e) {
        throw CollectorError(-1, "apply_plan", "", revision, e.what());
    }
}

void Collector::set_event_callback(py::object cb) {
    try {
        // 存储回调对象，保持强引用以防被 GC 回收
        // callback_ = cb;
    } catch (const std::exception& e) {
        throw CollectorError(-1, "set_event_callback", "", -1, e.what());
    }
}

std::pair<int, py::list> Collector::snapshot_since(int since_seq) {
    try {
        py::gil_scoped_acquire acquire_gil;
        // 仅持 GIL 拷贝数据，不做 IO 等待
        py::list samples;
        // TODO: 从无锁环形缓冲读取数据并转换为 py::dict 加入 samples
        return {since_seq, samples};
    } catch (const std::exception& e) {
        throw CollectorError(-1, "snapshot_since", "", -1, e.what());
    }
}

int Collector::write_point(const std::string& device_key, int point_id, const std::string& value) {
    try {
        py::gil_scoped_release release_gil;
        // TODO: 执行控制写入入队逻辑
        return 0;
    } catch (const std::exception& e) {
        throw CollectorError(-1, "write_point", device_key, point_id, e.what());
    }
}

void Collector::shutdown() {
    try {
        py::gil_scoped_release release_gil;
        impl_->running = false;
        // TODO: 停止调度、断开连接、释放资源
    } catch (const std::exception& e) {
        throw CollectorError(-1, "shutdown", "", -1, e.what());
    }
}

} // namespace ibmcp
