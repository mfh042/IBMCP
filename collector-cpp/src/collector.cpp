#include "collector/collector.h"

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
    catch (const std::exception& e) { throw CollectorInternalError(e.what()); }
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

int Collector::apply_plan(const std::map<std::string, std::string>& plan_json, int revision) {
    if (revision <= impl_->current_revision) return 0;
    impl_->current_revision = revision;
    return 0;
}

void Collector::set_event_callback(std::function<void(const std::map<std::string, std::string>&)> cb) {}
std::pair<int, std::vector<std::map<std::string, std::string>>> Collector::snapshot_since(int since_seq) {
    return {since_seq, {}};
}
int Collector::write_point(const std::string& device_key, int point_id, const std::string& value) { return 0; }
void Collector::shutdown() { impl_->running = false; }

} // namespace ibmcp
