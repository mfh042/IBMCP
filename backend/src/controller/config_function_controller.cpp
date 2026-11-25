#include "controller/config_function_controller.h"

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
constexpr const char *kFunctionsPattern = "cache:config:functions*";

Json::Value rowObj(const drogon::orm::Result &rows, size_t i) {
    return util::rowToJson(rows[i]);
}

}  // namespace

Task<HttpResponsePtr> ConfigFunctionController::list(HttpRequestPtr req) {
    auth::requireAuth(req);
    int64_t configId = ctrl::getInt64Param(req, "config_id", 0);
    std::string key = std::string(cache::kPrefix) + "config:functions:cfg:" +
                      std::to_string(configId);
    auto data = co_await cache::getOrSet(key, kConfigTtl, [&]() -> Task<Json::Value> {
        std::string where;
        if (configId > 0) where = "WHERE config_id = " + std::to_string(configId);
        std::string sql = "SELECT * FROM running_equipment_function " + where + " ORDER BY id";
        auto rows = co_await db::ro()->execSqlCoro(sql);
        Json::Value items = util::resultToJson(rows);
        Json::Value out(Json::objectValue);
        out["items"] = items;
        out["total"] = static_cast<Json::Int64>(items.size());
        co_return out;
    });
    co_return result::ok(data);
}

Task<HttpResponsePtr> ConfigFunctionController::create(HttpRequestPtr req) {
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    auto bodyPtr = req->getJsonObject();
    if (!bodyPtr) throw BizError(err::VALIDATION_FAILED, "request body must be a JSON object");
    const auto &body = *bodyPtr;

    auto data = ctrl::bodyToPairs(body,
        {"config_id", "function_code", "function_name", "function_type",
         "enabled", "function_params", "remark"});
    int64_t now = ctrl::nowEpoch();
    data.push_back({"created_at", std::to_string(now)});
    data.push_back({"updated_at", std::to_string(now)});
    auto built = ctrl::buildInsert("running_equipment_function", std::move(data));
    auto rows = co_await db::rw()->execSqlCoro(built);
    co_await cache::invalidateMatch(kFunctionsPattern);
    co_return result::ok(rowObj(rows, 0));
}

Task<HttpResponsePtr> ConfigFunctionController::update(HttpRequestPtr req, int64_t funcId) {
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    auto bodyPtr = req->getJsonObject();
    if (!bodyPtr) throw BizError(err::VALIDATION_FAILED, "request body must be a JSON object");
    const auto &body = *bodyPtr;

    auto exists = co_await db::rw()->execSqlCoro(
        "SELECT id FROM running_equipment_function WHERE id = $1", funcId);
    if (exists.empty()) throw BizError(err::NOT_FOUND, "function not found");

    auto data = ctrl::bodyToPairs(body,
        {"config_id", "function_code", "function_name", "function_type",
         "enabled", "function_params", "remark"});
    data.push_back({"updated_at", std::to_string(ctrl::nowEpoch())});
    auto sql = ctrl::buildUpdate("running_equipment_function", std::move(data), funcId);
    auto rows = co_await db::rw()->execSqlCoro(sql);
    co_await cache::invalidateMatch(kFunctionsPattern);
    co_return result::ok(rowObj(rows, 0));
}

Task<HttpResponsePtr> ConfigFunctionController::remove(HttpRequestPtr req, int64_t funcId) {
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    co_await db::rw()->execSqlCoro(
        "DELETE FROM running_equipment_function WHERE id = $1", funcId);
    co_await cache::invalidateMatch(kFunctionsPattern);

    Json::Value data(Json::objectValue);
    data["ok"] = true;
    co_return result::ok(data);
}

}  // namespace ibmcp