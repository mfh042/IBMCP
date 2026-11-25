#pragma once

#include "core/errors.h"

#include <collector/exports.h>
#include <nlohmann/json.hpp>

#include <string>

namespace ibmcp::collector_err {

inline int rcToCode(int rc) {
    switch (rc) {
        case 0: return 500;
        case 1: return 4001;
        case 2: return 4002;
        case 3: return 4003;
        case 4: return 4004;
        case 5: return 4005;
        case 6: return 4006;
        default: return 500;
    }
}

inline int classToRc(const char *name) {
    std::string n(name ? name : "");
    if (n == "ProtocolNotSupported") return 1;
    if (n == "InvalidConnection") return 2;
    if (n == "PlanRevisionConflict") return 3;
    if (n == "NotInitialized") return 4;
    if (n == "ControlQueueFull") return 5;
    if (n == "DeviceNotInPlan") return 6;
    return 0;
}

inline nlohmann::json toApiError(const ibmcp::CollectorError &e) {
    int rc = e.rc();
    if (rc == 0) rc = classToRc(typeid(e).name());
    nlohmann::json extra = nlohmann::json::object();
    if (!e.stage().empty()) extra["stage"] = e.stage();
    if (!e.device_key().empty()) extra["device_key"] = e.device_key();
    if (e.point_id() != 0) extra["point_id"] = e.point_id();
    nlohmann::json out;
    out["code"] = rcToCode(rc);
    out["message"] = e.what();
    out["extra"] = extra;
    return out;
}

inline void rethrowAsBiz(const std::exception &e) {
    auto *ce = dynamic_cast<const ibmcp::CollectorError *>(&e);
    if (ce) {
        auto info = toApiError(*ce);
        throw ibmcp::BizError(info["code"].get<int>(), info["message"].get<std::string>());
    }
    throw ibmcp::BizError(ibmcp::err::INTERNAL, e.what());
}

}  // namespace ibmcp::collector_err