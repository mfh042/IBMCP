#include "controller/control_controller.h"

#include <algorithm>

#include "controller/common.h"
#include "core/cache.h"
#include "core/result.h"
#include "db/db.h"
#include "service/control_service.h"

using namespace drogon;

namespace ibmcp {

namespace {

bool parseWaitFlag(const Json::Value &body) {
    const auto &w = body["wait"];
    if (w.isBool()) return w.asBool();
    if (w.isInt()) return w.asInt() == 1;
    if (w.isString()) {
        std::string s = w.asString();
        return s == "1" || s == "true" || s == "True";
    }
    return false;
}

double parseTimeout(const Json::Value &body) {
    const auto &t = body["timeout"];
    if (t.isNull()) return 3.0;
    try {
        if (t.isNumeric()) return t.asDouble();
        return std::stod(t.asString());
    } catch (...) {
        throw BizError(err::VALIDATION_FAILED, "timeout must be a number");
    }
}

}  // namespace

Task<HttpResponsePtr> ControlController::write(HttpRequestPtr req) {
    ctrl::requireAnyRole(req, {"engineer", "admin", "operator"});
    auto j = req->getJsonObject();
    if (!j) throw BizError(err::VALIDATION_FAILED, "request body required");
    const auto &body = *j;

    std::string deviceCode =
        body.isMember("device_code") && !body["device_code"].isNull()
            ? body["device_code"].asString()
            : (body.isMember("device_key") ? body["device_key"].asString() : "");
    if (deviceCode.empty() || !body.isMember("point_id") || body["point_id"].isNull() ||
        !body.isMember("value") || body["value"].isNull()) {
        throw BizError(err::VALIDATION_FAILED, "device_code, point_id, value required");
    }
    int pointId = body["point_id"].asInt();
    std::string value = body["value"].asString();
    bool wait = parseWaitFlag(body);
    double timeout = parseTimeout(body);
    if (wait && !(timeout > 0 && timeout <= 30)) {
        throw BizError(err::VALIDATION_FAILED, "timeout must be in (0, 30] seconds");
    }
    std::string pointName = body.isMember("point_name") && !body["point_name"].isNull()
                                ? body["point_name"].asString()
                                : "";

    service::ControlService svc(db::rw());
    auto data = co_await svc.write(deviceCode, pointId, value, pointName, wait,
                                   ctrl::operatorOf(req), timeout);
    co_await cache::invalidateMatch("cache:runtime:devices*");
    co_return result::ok(data);
}

Task<HttpResponsePtr> ControlController::getCommand(HttpRequestPtr req,
                                                    int64_t commandId) {
    auth::requireAuth(req);
    service::ControlService svc(db::ro());
    auto data = co_await svc.getCommand(commandId);
    co_return result::ok(data);
}

Task<HttpResponsePtr> ControlController::queueDepth(HttpRequestPtr req) {
    auth::requireAuth(req);
    service::ControlService svc(db::ro());
    co_return result::ok(svc.queueDepth());
}

}  // namespace ibmcp