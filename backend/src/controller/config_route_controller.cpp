#include "controller/config_route_controller.h"

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
constexpr const char *kRoutesPattern = "cache:config:routes*";

Json::Value rowObj(const drogon::orm::Result &rows, size_t i) {
    return util::rowToJson(rows[i]);
}

constexpr const char *kLevelL1 = "L1";

}  // namespace

Task<HttpResponsePtr> ConfigRouteController::list(HttpRequestPtr req) {
    auth::requireAuth(req);
    int64_t configId = ctrl::getInt64Param(req, "config_id", 0);
    std::string key = std::string(cache::kPrefix) + "config:routes:cfg:" +
                      std::to_string(configId);
    auto data = co_await cache::getOrSet(key, kConfigTtl, [&]() -> Task<Json::Value> {
        std::string where;
        if (configId > 0) where = "WHERE config_id = " + std::to_string(configId);
        std::string sql = "SELECT * FROM running_equipment_route " + where + " ORDER BY id";
        auto rows = co_await db::ro()->execSqlCoro(sql);
        Json::Value items = util::resultToJson(rows);
        Json::Value out(Json::objectValue);
        out["items"] = items;
        out["total"] = static_cast<Json::Int64>(items.size());
        co_return out;
    });
    co_return result::ok(data);
}

Task<HttpResponsePtr> ConfigRouteController::create(HttpRequestPtr req) {
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    auto bodyPtr = req->getJsonObject();
    if (!bodyPtr) throw BizError(err::VALIDATION_FAILED, "request body must be a JSON object");
    const auto &body = *bodyPtr;

    auto data = ctrl::bodyToPairs(body,
        {"config_id", "route_code", "route_name", "route_seq", "route_version",
         "route_params", "enabled"});
    int64_t now = ctrl::nowEpoch();
    data.push_back({"created_at", std::to_string(now)});
    data.push_back({"updated_at", std::to_string(now)});
    auto built = ctrl::buildInsert("running_equipment_route", std::move(data));
    auto rows = co_await db::rw()->execSqlCoro(built);
    co_await cache::invalidateMatch(kRoutesPattern);
    co_return result::ok(rowObj(rows, 0));
}

Task<HttpResponsePtr> ConfigRouteController::expand(HttpRequestPtr req, int64_t routeId) {
    auth::requireAuth(req);
    std::string key = std::string(cache::kPrefix) + "config:routes:" + std::to_string(routeId) +
                      ":expand";
    auto route = co_await cache::getOrSet(key, kConfigTtl, [&]() -> Task<Json::Value> {
        auto routeRows = co_await db::ro()->execSqlCoro(
            "SELECT * FROM running_equipment_route WHERE id = $1", routeId);
        if (routeRows.empty()) throw BizError(err::NOT_FOUND, "route not found");

        Json::Value routeOut = rowObj(routeRows, 0);

        auto bindings = co_await db::ro()->execSqlCoro(
            "SELECT * FROM running_route_device WHERE route_config_id = $1 ORDER BY id", routeId);
        Json::Value devicesArr(Json::arrayValue);
        for (const auto &b : util::resultToJson(bindings)) {
            int64_t bindingId = b["id"].asInt64();
            auto cols = co_await db::ro()->execSqlCoro(
                "SELECT * FROM running_device_collection WHERE device_binding_id = $1 ORDER BY id",
                bindingId);
            Json::Value dev = b;
            dev["collections"] = util::resultToJson(cols);
            devicesArr.append(dev);
        }
        routeOut["devices"] = devicesArr;
        co_return routeOut;
    });
    co_return result::ok(route);
}

Task<HttpResponsePtr> ConfigRouteController::update(HttpRequestPtr req, int64_t routeId) {
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    auto bodyPtr = req->getJsonObject();
    if (!bodyPtr) throw BizError(err::VALIDATION_FAILED, "request body must be a JSON object");
    const auto &body = *bodyPtr;

    auto exists = co_await db::rw()->execSqlCoro(
        "SELECT id FROM running_equipment_route WHERE id = $1", routeId);
    if (exists.empty()) throw BizError(err::NOT_FOUND, "route not found");

    auto data = ctrl::bodyToPairs(body,
        {"config_id", "route_code", "route_name", "route_seq", "route_version",
         "route_params", "enabled"});
    data.push_back({"updated_at", std::to_string(ctrl::nowEpoch())});
    auto sql = ctrl::buildUpdate("running_equipment_route", std::move(data), routeId);
    auto rows = co_await db::rw()->execSqlCoro(sql);
    co_await cache::invalidateMatch(kRoutesPattern);
    co_return result::ok(rowObj(rows, 0));
}

Task<HttpResponsePtr> ConfigRouteController::remove(HttpRequestPtr req, int64_t routeId) {
    ctrl::requireAnyRole(req, {"engineer", "admin"});

    co_await db::rw()->execSqlCoro(
        "DELETE FROM running_device_collection WHERE device_binding_id IN "
        "(SELECT id FROM running_route_device WHERE route_config_id = $1)",
        routeId);
    co_await db::rw()->execSqlCoro(
        "DELETE FROM running_route_device WHERE route_config_id = $1", routeId);
    co_await db::rw()->execSqlCoro("DELETE FROM running_equipment_route WHERE id = $1", routeId);
    co_await cache::invalidateMatch(kRoutesPattern);

    Json::Value data(Json::objectValue);
    data["ok"] = true;
    co_return result::ok(data);
}

}  // namespace ibmcp