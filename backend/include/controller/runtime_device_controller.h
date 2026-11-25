#pragma once

#include <drogon/HttpController.h>

namespace ibmcp {

class RuntimeDeviceController : public drogon::HttpController<RuntimeDeviceController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(RuntimeDeviceController::list, "/api/v1/runtime/devices", drogon::Get);
    ADD_METHOD_TO(RuntimeDeviceController::overview, "/api/v1/runtime/devices/overview",
                  drogon::Get);
    ADD_METHOD_TO(RuntimeDeviceController::get, "/api/v1/runtime/devices/{1}", drogon::Get);
    ADD_METHOD_TO(RuntimeDeviceController::points, "/api/v1/runtime/devices/{1}/points",
                  drogon::Get);
    ADD_METHOD_TO(RuntimeDeviceController::point, "/api/v1/runtime/devices/{1}/points/{2}",
                  drogon::Get);
    ADD_METHOD_TO(RuntimeDeviceController::control, "/api/v1/runtime/devices/{1}/control",
                  drogon::Post);
    ADD_METHOD_TO(RuntimeDeviceController::lastValues,
                  "/api/v1/runtime/devices/{1}/last-values", drogon::Get);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> list(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> overview(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> get(drogon::HttpRequestPtr req,
                                              std::string &&deviceCode);
    drogon::Task<drogon::HttpResponsePtr> points(drogon::HttpRequestPtr req,
                                                 std::string &&deviceCode);
    drogon::Task<drogon::HttpResponsePtr> point(drogon::HttpRequestPtr req,
                                                std::string &&deviceCode, int pointId);
    drogon::Task<drogon::HttpResponsePtr> control(drogon::HttpRequestPtr req,
                                                  std::string &&deviceCode);
    drogon::Task<drogon::HttpResponsePtr> lastValues(drogon::HttpRequestPtr req,
                                                     std::string &&deviceCode);
};

}  // namespace ibmcp