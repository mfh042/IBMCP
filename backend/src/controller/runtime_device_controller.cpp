#include "controller/runtime_device_controller.h"

#include <algorithm>
#include <set>

#include "controller/common.h"
#include "core/auth.h"
#include "core/cache.h"
#include "core/errors.h"
#include "core/result.h"
#include "db/db.h"
#include "service/control_service.h"
#include "util/json_util.h"

using namespace drogon;

namespace ibmcp {

Task<HttpResponsePtr> RuntimeDeviceController::list(HttpRequestPtr req) {
    auth::requireAuth(req);
    int page = std::max(1, ctrl::getIntParam(req, "page", 1));
    int size = std::clamp(ctrl::getIntParam(req, "size", 20), 1, 200);
    bool hasStatus = !req->getParameter("status").empty();
    int status = hasStatus ? ctrl::getIntParam(req, "status", 0) : 0;
    std::string key = std::string(cache::kPrefix) + "runtime:devices:p" +
                      std::to_string(page) + ":s" + std::to_string(size) + ":st" +
                      std::to_string(hasStatus ? status : -1);
    auto data = co_await cache::getOrSet(key, 30, [&]() -> Task<Json::Value> {
        std::string where;
        if (hasStatus) where = "WHERE status = " + std::to_string(status);
        int64_t total = 0;
        {
            std::string cntSql = "SELECT COUNT(*)::bigint AS c FROM using_device_runtime " + where;
            auto cnt = co_await db::ro()->execSqlCoro(cntSql);
            if (!cnt.empty()) total = cnt[0]["c"].as<int64_t>();
        }
        std::string sql = "SELECT * FROM using_device_runtime " + where +
                          " ORDER BY device_key LIMIT " + std::to_string(size) + " OFFSET " +
                          std::to_string((page - 1) * size);
        auto rows = co_await db::ro()->execSqlCoro(sql);
        Json::Value out(Json::objectValue);
        out["items"] = util::resultToJson(rows);
        out["total"] = static_cast<Json::Int64>(total);
        co_return out;
    });
    co_return result::ok(data);
}

Task<HttpResponsePtr> RuntimeDeviceController::overview(HttpRequestPtr req) {
    auth::requireAuth(req);
    std::string key = std::string(cache::kPrefix) + "runtime:devices:overview";
    auto data = co_await cache::getOrSet(key, 30, [&]() -> Task<Json::Value> {
        auto total = co_await db::ro()->execSqlCoro(
            "SELECT COUNT(*)::bigint AS c FROM using_device_runtime");
        auto online = co_await db::ro()->execSqlCoro(
            "SELECT COUNT(*)::bigint AS c FROM using_device_runtime WHERE status = 1");
        auto alarm = co_await db::ro()->execSqlCoro(
            "SELECT COUNT(*)::bigint AS c FROM using_alarms WHERE state = 1");
        int64_t t = total.empty() ? 0 : total[0]["c"].as<int64_t>();
        int64_t on = online.empty() ? 0 : online[0]["c"].as<int64_t>();
        int64_t al = alarm.empty() ? 0 : alarm[0]["c"].as<int64_t>();
        Json::Value out(Json::objectValue);
        out["device_total"] = static_cast<Json::Int64>(t);
        out["device_online"] = static_cast<Json::Int64>(on);
        out["device_offline"] = static_cast<Json::Int64>(t - on);
        out["active_alarm_count"] = static_cast<Json::Int64>(al);
        co_return out;
    });
    co_return result::ok(data);
}

Task<HttpResponsePtr> RuntimeDeviceController::get(HttpRequestPtr req,
                                                   std::string &&deviceCode) {
    auth::requireAuth(req);
    auto rows = co_await db::ro()->execSqlCoro(
        "SELECT * FROM using_device_runtime WHERE device_key = $1", deviceCode);
    if (rows.empty()) {
        Json::Value d(Json::objectValue);
        d["device_key"] = deviceCode;
        d["status"] = 0;
        d["last_error"] = Json::Value();
        d["last_seen"] = Json::Value();
        co_return result::ok(d);
    }
    co_return result::ok(util::resultToJson(rows)[0]);
}

Task<HttpResponsePtr> RuntimeDeviceController::points(HttpRequestPtr req,
                                                      std::string &&deviceCode) {
    auth::requireAuth(req);
    auto rows = co_await db::ro()->execSqlCoro(
        "SELECT * FROM using_points WHERE device_code = $1 ORDER BY id", deviceCode);
    auto arr = util::resultToJson(rows);
    co_return result::paginated(static_cast<int64_t>(arr.size()), arr);
}

Task<HttpResponsePtr> RuntimeDeviceController::point(HttpRequestPtr req,
                                                     std::string &&deviceCode, int pointId) {
    auth::requireAuth(req);
    auto rows = co_await db::ro()->execSqlCoro(
        "SELECT * FROM using_points WHERE device_code = $1 AND id = $2", deviceCode, pointId);
    if (rows.empty()) throw BizError(err::NOT_FOUND, "point not found");
    co_return result::ok(util::resultToJson(rows)[0]);
}

Task<HttpResponsePtr> RuntimeDeviceController::control(HttpRequestPtr req,
                                                       std::string &&deviceCode) {
    ctrl::requireAnyRole(req, {"engineer", "admin", "operator"});
    auto j = req->getJsonObject();
    if (!j) throw BizError(err::VALIDATION_FAILED, "request body required");
    const auto &body = *j;
    if (!body.isMember("point_id") || body["point_id"].isNull() || !body.isMember("value") ||
        body["value"].isNull())
        throw BizError(err::VALIDATION_FAILED, "point_id and value required");
    int pointId = body["point_id"].asInt();
    std::string value = body["value"].asString();
    std::string pointName = body.isMember("point_name") && !body["point_name"].isNull()
                                ? body["point_name"].asString()
                                : "";
    service::ControlService svc(db::rw());
    auto data = co_await svc.write(deviceCode, pointId, value, pointName);
    co_await cache::invalidateMatch("cache:runtime:devices*");
    co_return result::ok(data);
}

Task<HttpResponsePtr> RuntimeDeviceController::lastValues(HttpRequestPtr req,
                                                          std::string &&deviceCode) {
    auth::requireAuth(req);
    int limit = std::clamp(ctrl::getIntParam(req, "limit", 50), 1, 500);
    auto rows = co_await db::ro()->execSqlCoro(
        "SELECT t.point_id, t.value, t.quality, EXTRACT(EPOCH FROM t.ts)::bigint AS ts "
        "FROM time_series_data t JOIN using_points p ON p.id = t.point_id "
        "WHERE p.device_code = $1 ORDER BY t.ts DESC LIMIT $2",
        deviceCode, limit);
    Json::Value items(Json::arrayValue);
    std::set<int64_t> seen;
    for (const auto &row : rows) {
        int64_t pid = row["point_id"].as<int64_t>();
        if (seen.count(pid)) continue;
        seen.insert(pid);
        Json::Value obj(Json::objectValue);
        obj["point_id"] = static_cast<Json::Int64>(pid);
        obj["value"] = util::fieldToJson(row["value"]);
        obj["quality"] = util::fieldToJson(row["quality"]);
        obj["ts"] = static_cast<Json::Int64>(row["ts"].as<int64_t>());
        items.append(obj);
    }
    Json::Value data(Json::objectValue);
    data["items"] = items;
    data["device_code"] = deviceCode;
    co_return result::ok(data);
}

}  // namespace ibmcp