#pragma once

#include <drogon/HttpController.h>

namespace ibmcp {

class ConfigFunctionController : public drogon::HttpController<ConfigFunctionController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ConfigFunctionController::list, "/api/v1/config/functions", drogon::Get);
    ADD_METHOD_TO(ConfigFunctionController::create, "/api/v1/config/functions", drogon::Post);
    ADD_METHOD_TO(ConfigFunctionController::update, "/api/v1/config/functions/{1}", drogon::Put);
    ADD_METHOD_TO(ConfigFunctionController::remove, "/api/v1/config/functions/{1}", drogon::Delete);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> list(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> create(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> update(drogon::HttpRequestPtr req,
                                                  int64_t funcId);
    drogon::Task<drogon::HttpResponsePtr> remove(drogon::HttpRequestPtr req,
                                                  int64_t funcId);
};

}  // namespace ibmcp