#include "service/control_service.h"

#include <algorithm>
#include <chrono>
#include <future>

#include <drogon/drogon.h>

#include "collector/collector_bridge.h"
#include "control/command_waiter.h"
#include "core/app_context.h"
#include "core/errors.h"
#include "util/json_util.h"

namespace ibmcp::service {

namespace {

int64_t nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

void ControlService::validateValue(const std::string &value, const std::string &dataType) {
    if (dataType.empty()) return;
    std::string dt = dataType;
    std::transform(dt.begin(), dt.end(), dt.begin(), ::tolower);
    auto fail = [&] {
        Json::Value d(Json::objectValue);
        d["data_type"] = dataType;
        d["value"] = value;
        throw BizError(err::BAD_REQUEST, "value '" + value + "' not parsable as " + dataType, d);
    };
    try {
        if (dt == "int" || dt == "long") {
            size_t p = 0;
            std::stoll(value, &p);
            if (p != value.size()) fail();
        } else if (dt == "float" || dt == "double") {
            size_t p = 0;
            std::stod(value, &p);
            if (p != value.size()) fail();
        } else if (dt == "bool") {
            std::string v = value;
            std::transform(v.begin(), v.end(), v.begin(), ::tolower);
            if (v != "0" && v != "1" && v != "true" && v != "false" && v != "yes" && v != "no")
                fail();
        }
    } catch (const BizError &) {
        throw;
    } catch (...) {
        fail();
    }
}

drogon::Task<Json::Value> ControlService::write(const std::string &deviceCode, int pointId,
                                                const std::string &value,
                                                const std::string &pointName, bool wait,
                                                const std::string &issuedBy, double timeoutSec) {
    auto binding = co_await db_->execSqlCoro(
        "SELECT rc.id AS binding_id, rc.device_code, rc.protocol_code, "
        "col.id AS collection_id, "
        "COALESCE(col.read_write, it.read_write, 'R') AS read_write, "
        "COALESCE(col.data_type, it.data_type, 'float') AS data_type, "
        "col.item_code, p.id AS using_point_id "
        "FROM running_route_device rc "
        "JOIN running_device_collection col ON col.device_binding_id = rc.id "
        "JOIN base_device_data_item_dict it ON it.item_code = col.item_code "
        "LEFT JOIN using_points p ON p.device_code = rc.device_code AND p.using_point_id = col.id "
        "WHERE rc.device_code = $1 AND col.id = $2 AND rc.enabled = 1 AND col.enabled = 1",
        deviceCode, pointId);
    if (binding.empty()) {
        throw BizError(err::NOT_FOUND,
                       "device/point not in running config: " + deviceCode + ":" +
                           std::to_string(pointId));
    }
    const auto &row = binding[0];
    if (row["using_point_id"].isNull()) {
        Json::Value d(Json::objectValue);
        d["device_code"] = deviceCode;
        d["point_id"] = pointId;
        throw BizError(err::NOT_FOUND,
                       "point " + std::to_string(pointId) +
                           " has no runtime snapshot, activate config first",
                       d);
    }
    int usingPointId = row["using_point_id"].as<int>();
    std::string readWrite = row["read_write"].as<std::string>();
    std::transform(readWrite.begin(), readWrite.end(), readWrite.begin(), ::toupper);
    if (readWrite != "W" && readWrite != "RW") {
        Json::Value d(Json::objectValue);
        d["point_id"] = pointId;
        throw BizError(4101, "point is read-only (read_write=" + readWrite + ")", d);
    }
    std::string dataType = row["data_type"].as<std::string>();

    auto rt = co_await db_->execSqlCoro("SELECT status FROM using_devices WHERE device_code = $1",
                                        deviceCode);
    if (rt.empty() || rt[0]["status"].as<int>() != 1) {
        Json::Value d(Json::objectValue);
        d["device_code"] = deviceCode;
        throw BizError(4103, "device " + deviceCode + " is offline", d);
    }

    validateValue(value, dataType);

    auto func = co_await db_->execSqlCoro(
        "SELECT 1 FROM running_equipment_function f "
        "JOIN running_equipment_config c ON c.id = f.config_id "
        "JOIN running_equipment_route r ON r.config_id = c.id "
        "JOIN running_route_device rc ON rc.route_config_id = r.id "
        "WHERE rc.device_code = $1 AND f.function_type = '控制' AND f.enabled = 1 LIMIT 1",
        deviceCode);
    if (func.empty()) {
        Json::Value d(Json::objectValue);
        d["device_code"] = deviceCode;
        throw BizError(4104, "no active control function bound to device " + deviceCode, d);
    }

    auto &bridge = AppContext::instance().collector();
    auto &waiter = control::CommandWaiter::instance();
    std::shared_future<nlohmann::json> fut;
    if (wait) fut = waiter.expect(deviceCode, pointId);

    int cmdId = 0;
    try {
        cmdId = bridge.writePoint(deviceCode, pointId, value);
    } catch (const std::exception &e) {
        if (wait) {
            nlohmann::json rej = {{"event", "write_result"},
                                  {"success", false},
                                  {"command_id", 0},
                                  {"device_code", deviceCode},
                                  {"point_id", pointId},
                                  {"message", std::string(e.what())}};
            waiter.resolve(deviceCode, pointId, rej);
        }
        throw BizError(err::INTERNAL,
                       std::string("write rejected by collector: ") + e.what());
    }
    if (cmdId <= 0) {
        if (wait) {
            nlohmann::json rej = {{"event", "write_result"},
                                  {"success", false},
                                  {"command_id", 0},
                                  {"device_code", deviceCode},
                                  {"point_id", pointId},
                                  {"message", "collector returned invalid command id"}};
            waiter.resolve(deviceCode, pointId, rej);
        }
        throw BizError(err::INTERNAL, "write rejected by collector: invalid command id");
    }

    std::string pname = pointName.empty() ? ("point-" + std::to_string(pointId)) : pointName;
    co_await db_->execSqlCoro(
        "INSERT INTO control_commands (device_id, point_id, point_name, value, status, "
        "issued_at, issued_by, ext_command_id) "
        "SELECT d.id, $2, $3, $4, 0, $5, $6, $7 FROM using_devices d WHERE d.device_code = $1",
        deviceCode, usingPointId, pname, value, nowEpoch(), issuedBy, cmdId);

    if (!wait) {
        Json::Value r(Json::objectValue);
        r["command_id"] = cmdId;
        r["accepted"] = true;
        r["message"] = "queued";
        co_return r;
    }

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::duration<double>(timeoutSec);
    bool got = false;
    nlohmann::json payload;
    while (std::chrono::steady_clock::now() < deadline) {
        if (fut.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            payload = fut.get();
            got = true;
            break;
        }
        co_await drogon::sleepCoro(drogon::app().getLoop(), 0.05);
    }
    if (!got) {
        nlohmann::json timeoutRes = {{"event", "write_result"},
                                     {"success", false},
                                     {"command_id", cmdId},
                                     {"device_code", deviceCode},
                                     {"point_id", pointId},
                                     {"message", "timeout"}};
        waiter.resolve(deviceCode, pointId, timeoutRes);
        Json::Value d(Json::objectValue);
        d["command_id"] = cmdId;
        d["device_code"] = deviceCode;
        d["point_id"] = pointId;
        throw BizError(4102, "control command timed out after " + std::to_string(timeoutSec) + "s",
                       d);
    }

    Json::Value r(Json::objectValue);
    r["command_id"] = cmdId;
    r["accepted"] = true;
    r["finished"] = true;
    auto merged = util::fromNlohmann(payload);
    if (merged.isObject()) {
        for (const auto &k : merged.getMemberNames()) r[k] = merged[k];
    }
    co_return r;
}

drogon::Task<Json::Value> ControlService::getCommand(int64_t commandId) {
    auto rows = co_await db_->execSqlCoro(
        "SELECT * FROM control_commands WHERE ext_command_id = $1 ORDER BY id DESC LIMIT 1",
        commandId);
    if (rows.empty()) {
        throw BizError(err::NOT_FOUND,
                       "command " + std::to_string(commandId) + " not found");
    }
    co_return util::resultToJson(rows)[0];
}

Json::Value ControlService::queueDepth() {
    auto h = AppContext::instance().collector().health();
    Json::Value r(Json::objectValue);
    r["queue_depth"] = h.queue_depth;
    r["revision"] = h.revision;
    return r;
}

}  // namespace ibmcp::service