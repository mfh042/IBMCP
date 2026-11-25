#include "controller/collector_admin_controller.h"

#include "collector/orchestrator.h"
#include "controller/common.h"
#include "core/app_context.h"
#include "core/config.h"
#include "core/result.h"

using namespace drogon;

namespace ibmcp {

Task<HttpResponsePtr> CollectorAdminController::health(HttpRequestPtr req) {
    auth::requireAuth(req);
    co_return result::ok(ctrl::healthToJson(AppContext::instance().collector().health()));
}

Task<HttpResponsePtr> CollectorAdminController::protocols(HttpRequestPtr req) {
    auth::requireAuth(req);
    Json::Value data(Json::objectValue);
    Json::Value arr(Json::arrayValue);
    for (const auto &p : AppContext::instance().collector().protocols()) arr.append(p);
    data["protocols"] = arr;
    co_return result::ok(data);
}

Task<HttpResponsePtr> CollectorAdminController::stats(HttpRequestPtr req) {
    auth::requireAuth(req);
    auto h = AppContext::instance().collector().health();
    Json::Value data(Json::objectValue);
    data["revision"] = h.revision;
    data["device_count"] = h.device_count;
    data["point_count"] = h.point_count;
    data["queue_depth"] = h.queue_depth;
    data["uptime_sec"] = h.uptime_sec;
    co_return result::ok(data);
}

Task<HttpResponsePtr> CollectorAdminController::reload(HttpRequestPtr req) {
    ctrl::requireAnyRole(req, {"admin"});
    auto &col = AppContext::instance().collector();
    col.shutdown();
    col.init(config::collectorCfg());
    try {
        co_await AppContext::instance().orchestrator().bootstrap();
    } catch (const std::exception &e) {
        LOG_ERROR << "reload bootstrap failed: " << e.what();
    }
    Json::Value data(Json::objectValue);
    data["ok"] = true;
    data["message"] = "collector reloaded";
    co_return result::ok(data);
}

}  // namespace ibmcp