#pragma once

#include <drogon/HttpController.h>

namespace ibmcp {

class ConfigDeviceController : public drogon::HttpController<ConfigDeviceController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ConfigDeviceController::list, "/api/v1/config/devices", drogon::Get);
    ADD_METHOD_TO(ConfigDeviceController::create, "/api/v1/config/devices", drogon::Post);
    ADD_METHOD_TO(ConfigDeviceController::update, "/api/v1/config/devices/{1}", drogon::Put);
    ADD_METHOD_TO(ConfigDeviceController::remove, "/api/v1/config/devices/{1}", drogon::Delete);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> list(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> create(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> update(drogon::HttpRequestPtr req,
                                                  int64_t bindingId);
    drogon::Task<drogon::HttpResponsePtr> remove(drogon::HttpRequestPtr req,
                                                  int64_t bindingId);
};

}  // namespace ibmcp