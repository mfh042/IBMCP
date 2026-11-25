#include "plan.h"
#include "collector/exports.h"

#include <string>
#include <utility>

namespace ibmcp {
namespace {

[[noreturn]] void fail(const std::string& msg, const std::string& device_key = "",
                       int64_t point_id = -1) {
    throw InvalidConnection(2, "apply_plan.validate", device_key, point_id, msg);
}

std::string req_str(const nlohmann::json& j, const char* key, const std::string& ctx) {
    if (!j.contains(key) || !j[key].is_string()) {
        fail("missing or non-string field '" + std::string(key) + "' in " + ctx);
    }
    return j[key].get<std::string>();
}

int req_int(const nlohmann::json& j, const char* key, const std::string& ctx) {
    if (!j.contains(key) || !j[key].is_number_integer()) {
        fail("missing or non-integer field '" + std::string(key) + "' in " + ctx);
    }
    return j[key].get<int>();
}

} // namespace

Plan parse_plan(const nlohmann::json& plan_json) {
    if (!plan_json.is_object()) {
        fail("plan must be a dict");
    }
    Plan plan;
    plan.revision = plan_json.value("revision", 0);
    plan.flush_interval_ms = plan_json.value("flush_interval_ms", 200);
    plan.plan_hash = plan_json.value("plan_hash", std::string());

    if (!plan_json.contains("devices") || !plan_json["devices"].is_array()) {
        fail("plan.devices must be a list");
    }
    for (const auto& dj : plan_json["devices"]) {
        if (!dj.is_object()) {
            fail("device entry must be an object");
        }
        Device dev;
        dev.device_key = req_str(dj, "device_key", "device");
        dev.device_name = dj.value("device_name", dev.device_key);
        dev.equipment_code = dj.value("equipment_code", std::string());
        dev.equipment_name = dj.value("equipment_name", std::string());
        dev.route_code = dj.value("route_code", std::string());
        dev.step_code = dj.value("step_code", std::string());
        dev.protocol = dj.value("protocol", std::string());
        dev.connection = dj.value("connection", nlohmann::json::object());
        if (!dev.connection.is_object()) {
            dev.connection = nlohmann::json::object();
        }
        dev.connect_timeout_ms = dj.value("connect_timeout_ms", 3000);
        dev.read_timeout_ms = dj.value("read_timeout_ms", 1000);

        if (dj.contains("points") && dj["points"].is_array()) {
            for (const auto& pj : dj["points"]) {
                if (!pj.is_object()) {
                    fail("point entry must be an object", dev.device_key);
                }
                const std::string ctx = "point on device '" + dev.device_key + "'";
                Point pt;
                pt.point_id = req_int(pj, "point_id", ctx);
                pt.point_key = pj.value("point_key", std::string());
                pt.item_code = pj.value("item_code", std::string());
                pt.item_name = pj.value("item_name", std::string());
                pt.address = req_str(pj, "address", ctx);
                pt.data_type = pj.value("data_type", std::string("float"));
                pt.read_write = pj.value("read_write", std::string("R"));
                pt.cycle_ms = pj.value("cycle_ms", 1000);
                pt.scale = pj.value("scale", 1.0);
                pt.offset = pj.value("offset", 0.0);
                pt.alarm_high = pj.value("alarm_high", 0.0);
                pt.alarm_low = pj.value("alarm_low", 0.0);
                pt.alarm_enabled = pj.value("alarm_enabled", false);
                pt.unit = pj.value("unit", std::string());
                dev.points.push_back(std::move(pt));
            }
        }
        plan.devices.push_back(std::move(dev));
    }
    return plan;
}

} // namespace ibmcp