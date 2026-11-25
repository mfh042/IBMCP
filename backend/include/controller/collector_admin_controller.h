#pragma once

#include <drogon/HttpController.h>

namespace ibmcp {

class CollectorAdminController : public drogon::HttpController<CollectorAdminController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(CollectorAdminController::health, "/api/v1/collector/health", drogon::Get);
    ADD_METHOD_TO(CollectorAdminController::protocols, "/api/v1/collector/protocols", drogon::Get);
    ADD_METHOD_TO(CollectorAdminController::stats, "/api/v1/collector/stats", drogon::Get);
    ADD_METHOD_TO(CollectorAdminController::reload, "/api/v1/collector/reload", drogon::Post);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> health(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> protocols(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> stats(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> reload(drogon::HttpRequestPtr req);
};

}  // namespace ibmcp