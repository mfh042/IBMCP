#include "controller/config_controller.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "collector/orchestrator.h"
#include "controller/common.h"
#include "core/app_context.h"
#include "core/auth.h"
#include "core/errors.h"
#include "core/result.h"
#include "db/db.h"
#include "service/audit_service.h"
#include "util/json_util.h"

using namespace drogon;

namespace ibmcp {

namespace {

constexpr int kConfigActive = 3001;
constexpr int kDeprecated = 410;
constexpr const char *kLevelL1 = "L1";

void rejectManagedFields(const Json::Value &body) {
    Json::Value bad(Json::arrayValue);
    if (body.isMember("config_status")) bad.append("config_status");
    if (body.isMember("run_status")) bad.append("run_status");
    if (!bad.empty()) {
        Json::Value data(Json::objectValue);
        data["fields"] = bad;
        throw BizError(err::VALIDATION_FAILED,
                       "field(s) managed by server, use activate/unload instead", data);
    }
}

Json::Value rowObj(const drogon::orm::Result &rows, size_t i) {
    return util::rowToJson(rows[i]);
}

}  // namespace

Task<HttpResponsePtr> ConfigController::listEquipments(HttpRequestPtr req) {
    auth::requireAuth(req);
    int page = std::max(1, ctrl::getIntParam(req, "page", 1));
    int size = std::clamp(ctrl::getIntParam(req, "size", 20), 1, 200);
    std::string keyword = req->getParameter("keyword");

    std::string where;
    std::string kwArg = keyword.empty() ? std::string() : ("%" + keyword + "%");
    if (!keyword.empty()) {
        where = "WHERE config_code ILIKE " + ctrl::quoteSql(kwArg) + " OR equipment_name ILIKE " +
                ctrl::quoteSql(kwArg);
    }
    int64_t total = 0;
    {
        std::string cntSql = "SELECT COUNT(*)::bigint AS c FROM running_equipment_config " + where;
        auto cnt = co_await db::ro()->execSqlCoro(cntSql);
        if (!cnt.empty()) total = cnt[0]["c"].as<int64_t>();
    }
    std::string sql = "SELECT * FROM running_equipment_config " + where +
                      " ORDER BY id DESC LIMIT " + std::to_string(size) + " OFFSET " +
                      std::to_string((page - 1) * size);
    auto rows = co_await db::ro()->execSqlCoro(sql);
    co_return result::paginated(total, util::resultToJson(rows));
}

Task<HttpResponsePtr> ConfigController::equipmentByCode(HttpRequestPtr req,
                                                        std::string code) {
    auth::requireAuth(req);
    auto rows = co_await db::ro()->execSqlCoro(
        "SELECT * FROM running_equipment_config WHERE config_code = $1 LIMIT 1", code);
    if (rows.empty()) throw BizError(err::NOT_FOUND, "config not found: " + code);
    co_return result::ok(rowObj(rows, 0));
}

Task<HttpResponsePtr> ConfigController::getEquipment(HttpRequestPtr req, int64_t configId) {
    auth::requireAuth(req);
    auto rows = co_await db::ro()->execSqlCoro(
        "SELECT * FROM running_equipment_config WHERE id = $1", configId);
    if (rows.empty()) throw BizError(err::NOT_FOUND, "config not found");
    co_return result::ok(rowObj(rows, 0));
}

Task<HttpResponsePtr> ConfigController::equipmentStatus(HttpRequestPtr req,
                                                        int64_t configId) {
    auth::requireAuth(req);
    auto cfg = co_await db::ro()->execSqlCoro(
        "SELECT * FROM running_equipment_config WHERE id = $1", configId);
    if (cfg.empty()) throw BizError(err::NOT_FOUND, "equipment config not found");

    auto rev = co_await db::ro()->execSqlCoro(
        "SELECT * FROM runtime_plan_revision WHERE config_id = $1 OR config_id IS NULL "
        "ORDER BY revision DESC LIMIT 1",
        configId);
    Json::Value lastRevision = rev.empty() ? Json::Value() : rowObj(rev, 0);

    auto routeCnt = co_await db::ro()->execSqlCoro(
        "SELECT COUNT(*)::bigint AS c FROM running_equipment_route WHERE config_id = $1",
        configId);
    auto deviceCnt = co_await db::ro()->execSqlCoro(
        "SELECT COUNT(*)::bigint AS c FROM running_route_device r "
        "JOIN running_equipment_route e ON e.id = r.route_config_id "
        "WHERE e.config_id = $1 AND r.enabled = 1",
        configId);
    auto pointCnt = co_await db::ro()->execSqlCoro(
        "SELECT COUNT(*)::bigint AS c FROM running_device_collection c "
        "JOIN running_route_device r ON r.id = c.device_binding_id "
        "JOIN running_equipment_route e ON e.id = r.route_config_id "
        "WHERE e.config_id = $1 AND c.enabled = 1",
        configId);

    Json::Value data(Json::objectValue);
    data["config"] = rowObj(cfg, 0);
    data["last_revision"] = lastRevision;
    data["route_count"] = routeCnt.empty() ? 0 : routeCnt[0]["c"].as<int64_t>();
    data["device_count"] = deviceCnt.empty() ? 0 : deviceCnt[0]["c"].as<int64_t>();
    data["point_count"] = pointCnt.empty() ? 0 : pointCnt[0]["c"].as<int64_t>();
    co_return result::ok(data);
}

Task<HttpResponsePtr> ConfigController::equipmentExpand(HttpRequestPtr req,
                                                        int64_t configId) {
    auth::requireAuth(req);
    auto cfg = co_await db::ro()->execSqlCoro(
        "SELECT * FROM running_equipment_config WHERE id = $1", configId);
    if (cfg.empty()) throw BizError(err::NOT_FOUND, "config not found");

    Json::Value data = rowObj(cfg, 0);

    auto inList = [](const std::vector<int64_t> &ids) {
        std::string out;
        for (size_t i = 0; i < ids.size(); ++i) {
            if (i) out += ",";
            out += std::to_string(ids[i]);
        }
        return out;
    };

    // Batch load the whole config tree in 3 queries (routes / devices /
    // collections) instead of 1 + R + R*D serial round-trips.
    auto routes = co_await db::ro()->execSqlCoro(
        "SELECT * FROM running_equipment_route WHERE config_id = $1 ORDER BY id", configId);
    Json::Value routesArr = util::resultToJson(routes);

    std::vector<int64_t> routeIds;
    routeIds.reserve(routesArr.size());
    for (const auto &r : routesArr) routeIds.push_back(r["id"].asInt64());

    std::map<int64_t, Json::Value> devicesByRoute;
    std::vector<int64_t> bindingIds;
    if (!routeIds.empty()) {
        auto devices = co_await db::ro()->execSqlCoro(
            "SELECT * FROM running_route_device WHERE route_config_id IN (" + inList(routeIds) +
            ") ORDER BY id");
        for (const auto &d : util::resultToJson(devices)) {
            int64_t rcId = d["route_config_id"].asInt64();
            devicesByRoute[rcId].append(d);
            bindingIds.push_back(d["id"].asInt64());
        }
    }

    std::map<int64_t, Json::Value> colsByBinding;
    if (!bindingIds.empty()) {
        auto cols = co_await db::ro()->execSqlCoro(
            "SELECT * FROM running_device_collection WHERE device_binding_id IN (" +
            inList(bindingIds) + ") ORDER BY id");
        for (const auto &c : util::resultToJson(cols)) {
            colsByBinding[c["device_binding_id"].asInt64()].append(c);
        }
    }

    Json::Value outRoutes(Json::arrayValue);
    for (const auto &r : routesArr) {
        int64_t routeId = r["id"].asInt64();
        Json::Value devicesArr(Json::arrayValue);
        auto dit = devicesByRoute.find(routeId);
        if (dit != devicesByRoute.end()) {
            for (const auto &d : dit->second) {
                Json::Value dev = d;
                int64_t bid = d["id"].asInt64();
                auto cit = colsByBinding.find(bid);
                dev["collections"] = cit == colsByBinding.end()
                                         ? Json::Value(Json::arrayValue)
                                         : cit->second;
                devicesArr.append(dev);
            }
        }
        Json::Value route = r;
        route["devices"] = devicesArr;
        outRoutes.append(route);
    }
    data["routes"] = outRoutes;

    auto funcs = co_await db::ro()->execSqlCoro(
        "SELECT * FROM running_equipment_function WHERE config_id = $1 ORDER BY id", configId);
    data["functions"] = util::resultToJson(funcs);
    co_return result::ok(data);
}

Task<HttpResponsePtr> ConfigController::equipmentPlan(HttpRequestPtr req, int64_t configId) {
    auth::requireAuth(req);
    auto cfg = co_await db::ro()->execSqlCoro(
        "SELECT id FROM running_equipment_config WHERE id = $1", configId);
    if (cfg.empty()) throw BizError(err::NOT_FOUND, "equipment config not found");
    auto plan = co_await AppContext::instance().orchestrator().compileOnly(
        configId);
    co_return result::ok(util::fromNlohmann(plan));
}

Task<HttpResponsePtr> ConfigController::listRevisions(HttpRequestPtr req) {
    auth::requireAuth(req);
    int page = std::max(1, ctrl::getIntParam(req, "page", 1));
    int size = std::clamp(ctrl::getIntParam(req, "size", 20), 1, 200);
    auto cnt = co_await db::ro()->execSqlCoro(
        "SELECT COUNT(*)::bigint AS c FROM runtime_plan_revision");
    int64_t total = cnt.empty() ? 0 : cnt[0]["c"].as<int64_t>();
    auto rows = co_await db::ro()->execSqlCoro(
        "SELECT * FROM runtime_plan_revision ORDER BY revision DESC LIMIT $1 OFFSET $2",
        std::to_string(size), std::to_string((page - 1) * size));
    co_return result::paginated(total, util::resultToJson(rows));
}

Task<HttpResponsePtr> ConfigController::createEquipment(HttpRequestPtr req) {
    auth::requireAuth(req);
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    auto bodyPtr = req->getJsonObject();
    if (!bodyPtr) throw BizError(err::VALIDATION_FAILED, "request body must be a JSON object");
    const auto &body = *bodyPtr;
    rejectManagedFields(body);

    auto data = ctrl::bodyToPairs(body,
        {"config_code", "equipment_code", "equipment_name", "customer_code",
         "customer_name", "category_code", "location", "remark"});
    data.push_back({"config_status", "0"});
    data.push_back({"run_status", "0"});
    int64_t now = ctrl::nowEpoch();
    data.push_back({"created_at", std::to_string(now)});
    data.push_back({"updated_at", std::to_string(now)});
    auto built = ctrl::buildInsert("running_equipment_config", std::move(data));
    auto rows = co_await db::rw()->execSqlCoro(built);
    auto row = rowObj(rows, 0);

    Json::Value detail(Json::objectValue);
    detail["config_code"] = row.get("config_code", Json::Value()).asString();
    co_await service::writeConfigChange(kLevelL1, row["id"].asInt64(), "create",
                                        ctrl::operatorOf(req), detail);
    co_return result::ok(row);
}

Task<HttpResponsePtr> ConfigController::updateEquipment(HttpRequestPtr req,
                                                        int64_t configId) {
    auth::requireAuth(req);
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    auto bodyPtr = req->getJsonObject();
    if (!bodyPtr) throw BizError(err::VALIDATION_FAILED, "request body must be a JSON object");
    const auto &body = *bodyPtr;
    rejectManagedFields(body);

    auto exists = co_await db::rw()->execSqlCoro(
        "SELECT id FROM running_equipment_config WHERE id = $1", configId);
    if (exists.empty()) throw BizError(err::NOT_FOUND, "equipment config not found");

    auto data = ctrl::bodyToPairs(body,
        {"config_code", "equipment_code", "equipment_name", "customer_code",
         "customer_name", "category_code", "location", "remark"});
    data.push_back({"updated_at", std::to_string(ctrl::nowEpoch())});
    auto sql = ctrl::buildUpdate("running_equipment_config", std::move(data), configId);
    auto rows = co_await db::rw()->execSqlCoro(sql);

    Json::Value fields(Json::arrayValue);
    std::vector<std::string> keys;
    for (auto it = body.begin(); it != body.end(); ++it) keys.push_back(it.key().asString());
    std::sort(keys.begin(), keys.end());
    for (const auto &k : keys) fields.append(k);
    Json::Value detail(Json::objectValue);
    detail["fields"] = fields;
    co_await service::writeConfigChange(kLevelL1, configId, "update", ctrl::operatorOf(req),
                                        detail);
    co_return result::ok(rowObj(rows, 0));
}

Task<HttpResponsePtr> ConfigController::activateEquipment(HttpRequestPtr req,
                                                          int64_t configId) {
    auth::requireAuth(req);
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    auto r = co_await AppContext::instance().orchestrator().activateConfig(configId);
    int revision = r.value("revision", 0);
    Json::Value detail(Json::objectValue);
    detail["plan_hash"] = r.value("plan_hash", "");
    detail["skipped"] = r.value("skipped", false);
    co_await service::writeConfigChange(kLevelL1, configId, "activate", ctrl::operatorOf(req),
                                        detail, revision);
    co_return result::ok(util::fromNlohmann(r));
}

Task<HttpResponsePtr> ConfigController::unloadEquipment(HttpRequestPtr req,
                                                        int64_t configId) {
    auth::requireAuth(req);
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    auto r = co_await AppContext::instance().orchestrator().deactivateConfig(
        configId);
    int revision = r.value("revision", 0);
    Json::Value detail(Json::objectValue);
    detail["device_count"] = r.value("device_count", 0);
    co_await service::writeConfigChange(kLevelL1, configId, "deactivate", ctrl::operatorOf(req),
                                        detail, revision);
    co_return result::ok(util::fromNlohmann(r));
}

Task<HttpResponsePtr> ConfigController::recompileAll(HttpRequestPtr req) {
    auth::requireAuth(req);
    ctrl::requireAnyRole(req, {"admin"});
    auto r = co_await AppContext::instance().orchestrator().recompileAll();
    co_return result::ok(util::fromNlohmann(r));
}

Task<HttpResponsePtr> ConfigController::deleteEquipment(HttpRequestPtr req,
                                                        int64_t configId) {
    auth::requireAuth(req);
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    auto cfg = co_await db::rw()->execSqlCoro(
        "SELECT config_status FROM running_equipment_config WHERE id = $1", configId);
    if (cfg.empty()) throw BizError(err::NOT_FOUND, "config not found");
    int configStatus = cfg[0]["config_status"].as<int>();
    if (configStatus == 1)
        throw BizError(kConfigActive, "config is active, unload first");

    co_await db::rw()->execSqlCoro("DELETE FROM running_equipment_function WHERE config_id = $1",
                                   configId);
    co_await db::rw()->execSqlCoro(
        "DELETE FROM running_route_device WHERE route_config_id IN "
        "(SELECT id FROM running_equipment_route WHERE config_id = $1)",
        configId);
    co_await db::rw()->execSqlCoro("DELETE FROM running_equipment_route WHERE config_id = $1",
                                   configId);
    co_await db::rw()->execSqlCoro("DELETE FROM running_equipment_config WHERE id = $1",
                                   configId);

    Json::Value detail(Json::objectValue);
    detail["config_status"] = configStatus;
    co_await service::writeConfigChange(kLevelL1, configId, "delete", ctrl::operatorOf(req),
                                        detail);
    Json::Value data(Json::objectValue);
    data["ok"] = true;
    co_return result::ok(data);
}

Task<HttpResponsePtr> ConfigController::syncEquipment(HttpRequestPtr req, int64_t configId) {
    auth::requireAuth(req);
    ctrl::requireAnyRole(req, {"admin"});
    auto r = co_await AppContext::instance().orchestrator().recompileAll();
    int revision = r.value("revision", 0);
    Json::Value detail(Json::objectValue);
    detail["device_count"] = r.value("device_count", 0);
    co_await service::writeConfigChange(kLevelL1, configId, "sync", ctrl::operatorOf(req), detail,
                                        revision);
    Json::Value data = util::fromNlohmann(r);
    data["ok"] = true;
    co_return result::ok(data);
}

Task<HttpResponsePtr> ConfigController::cleanupEquipment(HttpRequestPtr req,
                                                         int64_t configId) {
    auth::requireAuth(req);
    ctrl::requireAnyRole(req, {"admin"});
    (void)configId;
    throw BizError(kDeprecated,
                   "deprecated: use POST /config/equipments/{id}/unload; historical data is kept");
}

}  // namespace ibmcp