#include "collector/collector_bridge.h"

#include <collector/collector.h>

#include <charconv>
#include <map>
#include <set>
#include <stdexcept>

namespace ibmcp {

std::string CollectorBridge::toStubProtocol(const std::string &name) {
    if (name == "modbustcp") return "modbus_tcp";
    if (name == "modbusrtu") return "modbus_rtu";
    if (name == "opcua") return "opc_ua";
    if (name == "sockettcp") return "socket_tcp";
    if (name == "socketudp") return "socket_udp";
    return name;
}

bool CollectorBridge::parseBool(const std::string &v) {
    std::string s = v;
    for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    return s == "true" || s == "1" || s == "yes";
}

int64_t CollectorBridge::parseInt(const std::string &v) {
    int64_t iv = 0;
    auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), iv);
    if (ec == std::errc() && p != v.data()) return iv;
    return 0;
}

double CollectorBridge::parseDouble(const std::string &v) {
    try {
        return std::stod(v);
    } catch (...) {
        return 0.0;
    }
}

namespace {
const std::set<std::string> &eventIntFields() {
    static const std::set<std::string> s = {
        "revision", "device_count", "point_count", "status", "point_id",
        "command_id", "ts_ms", "finished_at", "ts", "total_samples",
        "queue_depth", "dropped", "devices_online", "devices_degraded",
    };
    return s;
}
const std::set<std::string> &eventBoolFields() {
    static const std::set<std::string> s = {"success"};
    return s;
}
}  // namespace

nlohmann::json CollectorBridge::normalizeEvent(const std::map<std::string, std::string> &evt) {
    nlohmann::json out = nlohmann::json::object();
    for (const auto &[k, v] : evt) {
        if (eventBoolFields().count(k)) {
            out[k] = parseBool(v);
        } else if (eventIntFields().count(k)) {
            int64_t iv = parseInt(v);
            out[k] = iv;
        } else {
            out[k] = v;
        }
    }
    return out;
}

void CollectorBridge::init(const nlohmann::json &cfg) {
    std::map<std::string, std::string> m;
    if (cfg.is_object()) {
        for (auto it = cfg.begin(); it != cfg.end(); ++it) {
            if (it.value().is_string()) m[it.key()] = it.value().get<std::string>();
            else m[it.key()] = it.value().dump();
        }
    }
    Collector::instance().init(m);
}

HealthInfo CollectorBridge::health() {
    auto h = Collector::instance().health();
    HealthInfo out;
    auto get = [&](const std::string &k) { return h.count(k) ? h[k] : std::string(); };
    out.threads = parseInt(get("threads"));
    out.revision = parseInt(get("revision"));
    out.device_count = parseInt(get("device_count"));
    out.devices_real = parseInt(get("devices_real"));
    out.devices_degraded = parseInt(get("devices_degraded"));
    out.point_count = parseInt(get("point_count"));
    out.queue_depth = parseInt(get("queue_depth"));
    out.dropped = parseInt(get("dropped"));
    out.uptime_sec = parseInt(get("uptime_sec"));
    std::string proto = get("protocols");
    if (!proto.empty()) {
        size_t start = 0;
        while (start < proto.size()) {
            size_t comma = proto.find(',', start);
            std::string tok = (comma == std::string::npos) ? proto.substr(start) : proto.substr(start, comma - start);
            if (!tok.empty()) out.protocols.push_back(toStubProtocol(tok));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
    return out;
}

std::vector<std::string> CollectorBridge::protocols() {
    auto v = Collector::instance().list_protocols();
    std::vector<std::string> out;
    out.reserve(v.size());
    for (auto &p : v) out.push_back(toStubProtocol(p));
    return out;
}

nlohmann::json CollectorBridge::capabilities() {
    auto caps = Collector::instance().protocol_capabilities();
    nlohmann::json out = nlohmann::json::object();
    for (const auto &[k, v] : caps) {
        nlohmann::json entry;
        auto getStr = [&](const std::string &key) {
            auto it = v.find(key);
            if (it == v.end()) return std::string("0");
            return std::to_string(it->second);
        };
        entry["compiled"] = parseBool(getStr("compiled"));
        entry["supports_write"] = parseBool(getStr("supports_write"));
        entry["max_points_per_device"] = parseInt(getStr("max_points_per_device"));
        out[toStubProtocol(k)] = entry;
    }
    return out;
}

int CollectorBridge::applyPlan(const nlohmann::json &plan, int revision) {
    return Collector::instance().apply_plan(plan, revision);
}

std::pair<int64_t, std::vector<Sample>> CollectorBridge::snapshotSince(int64_t sinceSeq) {
    auto [newSeq, snapshots] = Collector::instance().snapshot_since(static_cast<int>(sinceSeq));
    std::vector<Sample> out;
    out.reserve(snapshots.size());
    for (const auto &s : snapshots) {
        Sample sm;
        auto get = [&](const std::string &k) -> std::string {
            auto it = s.find(k);
            if (it == s.end()) return {};
            return it->second;
        };
        sm.seq = parseInt(get("seq"));
        sm.point_id = parseInt(get("point_id"));
        sm.ts_ms = parseInt(get("ts_ms"));
        sm.value = parseDouble(get("value"));
        sm.quality = get("quality");
        sm.raw_value = get("raw_value");
        sm.device_key = get("device_key");
        out.push_back(std::move(sm));
    }
    return {static_cast<int64_t>(newSeq), std::move(out)};
}

int CollectorBridge::writePoint(const std::string &deviceKey, int pointId, const std::string &value) {
    return Collector::instance().write_point(deviceKey, pointId, value);
}

void CollectorBridge::setEventCallback(std::function<void(const nlohmann::json &)> cb) {
    userCb_ = std::move(cb);
    if (!userCb_) {
        Collector::instance().set_event_callback(nullptr);
        return;
    }
    Collector::instance().set_event_callback(
        [this](const std::map<std::string, std::string> &evt) {
            if (userCb_) userCb_(normalizeEvent(evt));
        });
}

void CollectorBridge::shutdown() {
    try {
        Collector::instance().set_event_callback(nullptr);
    } catch (...) {
    }
    Collector::instance().shutdown();
}

}  // namespace ibmcp