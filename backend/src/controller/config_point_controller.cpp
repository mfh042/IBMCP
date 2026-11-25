#include "controller/config_point_controller.h"

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
constexpr const char *kPointsPattern = "cache:config:points*";
// route expand 内嵌 collection points，point 写后必须失效，避免最长 300s 脏读。
constexpr const char *kRoutesPattern = "cache:config:routes*";

Json::Value rowObj(const drogon::orm::Result &rows, size_t i) {
    return util::rowToJson(rows[i]);
}

ctrl::KvList collectionPayload(const Json::Value &body) {
    static const std::set<std::string> kAllowed = {
        "device_binding_id", "device_code", "item_code", "item_name",
        "collection_cycle", "address_override", "data_type", "read_write",
        "scale", "offset", "alarm_high", "alarm_low", "alarm_enabled", "enabled"};
    ctrl::KvList out;
    for (auto it = body.begin(); it != body.end(); ++it) {
        std::string key = it.key().asString();
        if (key == "binding_id") key = "device_binding_id";
        if (kAllowed.count(key)) out.emplace_back(key, ctrl::jsonValueToParam(*it));
    }
    return out;
}

}  // namespace

Task<HttpResponsePtr> ConfigPointController::list(HttpRequestPtr req) {
    auth::requireAuth(req);
    int64_t bindingId = ctrl::getInt64Param(req, "binding_id", 0);
    std::string key = std::string(cache::kPrefix) + "config:points:binding:" +
                      std::to_string(bindingId);
    auto data = co_await cache::getOrSet(key, kConfigTtl, [&]() -> Task<Json::Value> {
        std::string where;
        if (bindingId > 0) where = "WHERE device_binding_id = " + std::to_string(bindingId);
        std::string sql = "SELECT * FROM running_device_collection " + where + " ORDER BY id";
        auto rows = co_await db::ro()->execSqlCoro(sql);
        Json::Value items = util::resultToJson(rows);
        Json::Value out(Json::objectValue);
        out["items"] = items;
        out["total"] = static_cast<Json::Int64>(items.size());
        co_return out;
    });
    co_return result::ok(data);
}

Task<HttpResponsePtr> ConfigPointController::create(HttpRequestPtr req) {
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    auto bodyPtr = req->getJsonObject();
    if (!bodyPtr) throw BizError(err::VALIDATION_FAILED, "request body must be a JSON object");
    const auto &body = *bodyPtr;

    auto data = collectionPayload(body);
    int64_t now = ctrl::nowEpoch();
    data.push_back({"created_at", std::to_string(now)});
    data.push_back({"updated_at", std::to_string(now)});
    auto built = ctrl::buildInsert("running_device_collection", std::move(data));
    auto rows = co_await db::rw()->execSqlCoro(built);
    co_await cache::invalidateMatch(kPointsPattern);
    co_await cache::invalidateMatch(kRoutesPattern);
    co_return result::ok(rowObj(rows, 0));
}

Task<HttpResponsePtr> ConfigPointController::update(HttpRequestPtr req, int64_t pointId) {
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    auto bodyPtr = req->getJsonObject();
    if (!bodyPtr) throw BizError(err::VALIDATION_FAILED, "request body must be a JSON object");
    const auto &body = *bodyPtr;

    auto exists = co_await db::rw()->execSqlCoro(
        "SELECT id FROM running_device_collection WHERE id = $1", pointId);
    if (exists.empty()) throw BizError(err::NOT_FOUND, "collection not found");

    auto data = collectionPayload(body);
    data.push_back({"updated_at", std::to_string(ctrl::nowEpoch())});
    auto sql = ctrl::buildUpdate("running_device_collection", std::move(data), pointId);
    auto rows = co_await db::rw()->execSqlCoro(sql);
    co_await cache::invalidateMatch(kPointsPattern);
    co_await cache::invalidateMatch(kRoutesPattern);
    co_return result::ok(rowObj(rows, 0));
}

Task<HttpResponsePtr> ConfigPointController::remove(HttpRequestPtr req, int64_t pointId) {
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    co_await db::rw()->execSqlCoro(
        "DELETE FROM running_device_collection WHERE id = $1", pointId);
    co_await cache::invalidateMatch(kPointsPattern);
    co_await cache::invalidateMatch(kRoutesPattern);

    Json::Value data(Json::objectValue);
    data["ok"] = true;
    co_return result::ok(data);
}

}  // namespace ibmcp