#pragma once

#include <drogon/HttpController.h>

namespace ibmcp {

class ConfigPointController : public drogon::HttpController<ConfigPointController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ConfigPointController::list, "/api/v1/config/points", drogon::Get);
    ADD_METHOD_TO(ConfigPointController::create, "/api/v1/config/points", drogon::Post);
    ADD_METHOD_TO(ConfigPointController::update, "/api/v1/config/points/{1}", drogon::Put);
    ADD_METHOD_TO(ConfigPointController::remove, "/api/v1/config/points/{1}", drogon::Delete);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> list(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> create(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> update(drogon::HttpRequestPtr req,
                                                  int64_t pointId);
    drogon::Task<drogon::HttpResponsePtr> remove(drogon::HttpRequestPtr req,
                                                  int64_t pointId);
};

}  // namespace ibmcp