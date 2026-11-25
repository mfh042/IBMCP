#pragma once

#include <drogon/HttpController.h>

namespace ibmcp {

class SystemController : public drogon::HttpController<SystemController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SystemController::ping, "/api/v1/ping", drogon::Get);
    ADD_METHOD_TO(SystemController::redisPing, "/api/v1/redis/ping", drogon::Get);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> ping(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> redisPing(drogon::HttpRequestPtr req);
};

}  // namespace ibmcp