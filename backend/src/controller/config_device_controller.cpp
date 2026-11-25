#include "controller/config_device_controller.h"

#include "controller/common.h"
#include "core/auth.h"
#include "core/cache.h"
#include "core/errors.h"
#include "core/result.h"
#include "db/db.h"
#include "util/json_util.h"

using namespace drogon;

namespace ibmcp {

namespace {

constexpr int kConfigTtl = 300;
constexpr const char *kDevicesPattern = "cache:config:devices*";
// route expand 内嵌 device bindings，device 写后必须失效，避免最长 300s 脏读。
constexpr const char *kRoutesPattern = "cache:config:routes*";

Json::Value rowObj(const drogon::orm::Result &rows, size_t i) {
    return util::rowToJson(rows[i]);
}

}  // namespace

Task<HttpResponsePtr> ConfigDeviceController::list(HttpRequestPtr req) {
    auth::requireAuth(req);
    int64_t routeId = ctrl::getInt64Param(req, "route_id", 0);
    std::string key = std::string(cache::kPrefix) + "config:devices:route:" +
                      std::to_string(routeId);
    auto data = co_await cache::getOrSet(key, kConfigTtl, [&]() -> Task<Json::Value> {
        std::string where;
        if (routeId > 0) where = "WHERE route_config_id = " + std::to_string(routeId);
        std::string sql = "SELECT * FROM running_route_device " + where + " ORDER BY id";
        auto rows = co_await db::ro()->execSqlCoro(sql);
        Json::Value items = util::resultToJson(rows);
        Json::Value out(Json::objectValue);
        out["items"] = items;
        out["total"] = static_cast<Json::Int64>(items.size());
        co_return out;
    });
    co_return result::ok(data);
}

Task<HttpResponsePtr> ConfigDeviceController::create(HttpRequestPtr req) {
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    auto bodyPtr = req->getJsonObject();
    if (!bodyPtr) throw BizError(err::VALIDATION_FAILED, "request body must be a JSON object");
    const auto &body = *bodyPtr;

    auto data = ctrl::bodyToPairs(body,
        {"route_config_id", "route_code", "step_code", "step_name", "step_seq",
         "device_code", "device_name", "device_connection", "port_code",
         "protocol_code", "enabled", "connect_timeout_ms", "read_timeout_ms"});
    int64_t now = ctrl::nowEpoch();
    data.push_back({"created_at", std::to_string(now)});
    data.push_back({"updated_at", std::to_string(now)});
    auto built = ctrl::buildInsert("running_route_device", std::move(data));
    auto rows = co_await db::rw()->execSqlCoro(built);
    co_await cache::invalidateMatch(kDevicesPattern);
    co_await cache::invalidateMatch(kRoutesPattern);
    co_return result::ok(rowObj(rows, 0));
}

Task<HttpResponsePtr> ConfigDeviceController::update(HttpRequestPtr req, int64_t bindingId) {
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    auto bodyPtr = req->getJsonObject();
    if (!bodyPtr) throw BizError(err::VALIDATION_FAILED, "request body must be a JSON object");
    const auto &body = *bodyPtr;

    auto exists = co_await db::rw()->execSqlCoro(
        "SELECT id FROM running_route_device WHERE id = $1", bindingId);
    if (exists.empty()) throw BizError(err::NOT_FOUND, "device binding not found");

    auto data = ctrl::bodyToPairs(body,
        {"route_config_id", "route_code", "step_code", "step_name", "step_seq",
         "device_code", "device_name", "device_connection", "port_code",
         "protocol_code", "enabled", "connect_timeout_ms", "read_timeout_ms"});
    data.push_back({"updated_at", std::to_string(ctrl::nowEpoch())});
    auto sql = ctrl::buildUpdate("running_route_device", std::move(data), bindingId);
    auto rows = co_await db::rw()->execSqlCoro(sql);
    co_await cache::invalidateMatch(kDevicesPattern);
    co_await cache::invalidateMatch(kRoutesPattern);
    co_return result::ok(rowObj(rows, 0));
}

Task<HttpResponsePtr> ConfigDeviceController::remove(HttpRequestPtr req, int64_t bindingId) {
    ctrl::requireAnyRole(req, {"engineer", "admin"});

    co_await db::rw()->execSqlCoro(
        "DELETE FROM running_device_collection WHERE device_binding_id = $1", bindingId);
    co_await db::rw()->execSqlCoro(
        "DELETE FROM running_route_device WHERE id = $1", bindingId);
    co_await cache::invalidateMatch(kDevicesPattern);
    co_await cache::invalidateMatch(kRoutesPattern);

    Json::Value data(Json::objectValue);
    data["ok"] = true;
    co_return result::ok(data);
}

}  // namespace ibmcp