#pragma once

#include <drogon/HttpController.h>

namespace ibmcp {

class ConfigRouteController : public drogon::HttpController<ConfigRouteController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ConfigRouteController::list, "/api/v1/config/routes", drogon::Get);
    ADD_METHOD_TO(ConfigRouteController::create, "/api/v1/config/routes", drogon::Post);
    ADD_METHOD_TO(ConfigRouteController::expand, "/api/v1/config/routes/{1}/expand", drogon::Get);
    ADD_METHOD_TO(ConfigRouteController::update, "/api/v1/config/routes/{1}", drogon::Put);
    ADD_METHOD_TO(ConfigRouteController::remove, "/api/v1/config/routes/{1}", drogon::Delete);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> list(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> create(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> expand(drogon::HttpRequestPtr req, int64_t routeId);
    drogon::Task<drogon::HttpResponsePtr> update(drogon::HttpRequestPtr req, int64_t routeId);
    drogon::Task<drogon::HttpResponsePtr> remove(drogon::HttpRequestPtr req, int64_t routeId);
};

}  // namespace ibmcp