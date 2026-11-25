#include "controller/alarm_controller.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

#include "controller/common.h"
#include "core/auth.h"
#include "core/cache.h"
#include "core/result.h"
#include "db/db.h"
#include "util/json_util.h"

using namespace drogon;

namespace ibmcp {

namespace {

int64_t nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

Task<HttpResponsePtr> AlarmController::list(HttpRequestPtr req) {
    auth::requireAuth(req);
    int page = std::max(1, ctrl::getIntParam(req, "page", 1));
    int64_t size = std::clamp(ctrl::getIntParam(req, "size", 20), 1, 200);
    bool hasState = !req->getParameter("state").empty();
    int16_t state = hasState ? static_cast<int16_t>(ctrl::getIntParam(req, "state", 0)) : 0;
    std::string deviceCode = ctrl::getStrParam(req, "device_code");

    std::string where;
    int pIdx = 0;
    bool haveState = false;
    bool haveDevice = false;
    if (hasState) {
        haveState = true;
        where = "WHERE state = $" + std::to_string(++pIdx);
    }
    if (!deviceCode.empty()) {
        haveDevice = true;
        std::string cond = "device_code = $" + std::to_string(++pIdx);
        where = where.empty() ? ("WHERE " + cond) : (where + " AND " + cond);
    }

    int64_t total = 0;
    {
        auto cntSql = "SELECT COUNT(*)::bigint AS c FROM using_alarms " + where;
        auto cnt = co_await ([&]() -> Task<drogon::orm::Result> {
            if (haveState && haveDevice) co_return co_await db::ro()->execSqlCoro(cntSql, state, deviceCode);
            if (haveState)               co_return co_await db::ro()->execSqlCoro(cntSql, state);
            if (haveDevice)              co_return co_await db::ro()->execSqlCoro(cntSql, deviceCode);
            co_return co_await db::ro()->execSqlCoro(cntSql);
        }());
        if (!cnt.empty()) total = cnt[0]["c"].as<int64_t>();
    }

    auto offset = (page - 1) * size;
    std::string listSql = "SELECT * FROM using_alarms " + where +
                          " ORDER BY triggered_at DESC LIMIT $" +
                          std::to_string(pIdx + 1) + " OFFSET $" +
                          std::to_string(pIdx + 2);

    auto rows = co_await ([&]() -> Task<drogon::orm::Result> {
        if (haveState && haveDevice) co_return co_await db::ro()->execSqlCoro(listSql, state, deviceCode, size, offset);
        if (haveState)               co_return co_await db::ro()->execSqlCoro(listSql, state, size, offset);
        if (haveDevice)              co_return co_await db::ro()->execSqlCoro(listSql, deviceCode, size, offset);
        co_return co_await db::ro()->execSqlCoro(listSql, size, offset);
    }());

    co_return result::paginated(total, util::resultToJson(rows));
}

Task<HttpResponsePtr> AlarmController::stats(HttpRequestPtr req) {
    auth::requireAuth(req);
    Json::Value data(Json::objectValue);
    for (auto [key, st] :
         {std::pair<const char *, int16_t>{"active", 1}, {"acknowledged", 2}, {"recovered", 3}}) {
        auto r = co_await db::ro()->execSqlCoro(
            "SELECT COUNT(*)::bigint AS c FROM using_alarms WHERE state = $1", st);
        data[key] = static_cast<Json::Int64>(r.empty() ? 0 : r[0]["c"].as<int64_t>());
    }
    co_return result::ok(data);
}

Task<HttpResponsePtr> AlarmController::get(HttpRequestPtr req, int64_t alarmId) {
    auth::requireAuth(req);
    auto rows = co_await db::ro()->execSqlCoro("SELECT * FROM using_alarms WHERE id = $1",
                                               alarmId);
    if (rows.empty()) throw BizError(err::NOT_FOUND, "alarm not found");
    co_return result::ok(util::resultToJson(rows)[0]);
}

Task<HttpResponsePtr> AlarmController::ack(HttpRequestPtr req, int64_t alarmId) {
    ctrl::requireAnyRole(req, {"operator", "engineer", "admin"});
    auto rows = co_await db::rw()->execSqlCoro("SELECT state FROM using_alarms WHERE id = $1",
                                               alarmId);
    if (rows.empty()) throw BizError(err::NOT_FOUND, "alarm not found");
    if (rows[0]["state"].as<int>() != 1)
        throw BizError(err::BAD_REQUEST, "only active alarms can be acknowledged");
    co_await db::rw()->execSqlCoro(
        "UPDATE using_alarms SET state = 2, acknowledged_at = $1 WHERE id = $2", nowEpoch(),
        alarmId);
    // overview 缓存含 active_alarm_count，ack 后必须失效避免最长 30s 脏读。
    co_await cache::invalidateMatch("cache:runtime:devices*");
    Json::Value d(Json::objectValue);
    d["ok"] = true;
    co_return result::ok(d);
}

Task<HttpResponsePtr> AlarmController::recover(HttpRequestPtr req, int64_t alarmId) {
    ctrl::requireAnyRole(req, {"operator", "engineer", "admin"});
    co_await db::rw()->execSqlCoro(
        "UPDATE using_alarms SET state = 3, recovered_at = $1 WHERE id = $2", nowEpoch(),
        alarmId);
    // overview 缓存含 active_alarm_count，recover 后必须失效避免最长 30s 脏读。
    co_await cache::invalidateMatch("cache:runtime:devices*");
    Json::Value d(Json::objectValue);
    d["ok"] = true;
    co_return result::ok(d);
}

}  // namespace ibmcp