#pragma once

#include <drogon/HttpController.h>

namespace ibmcp {

class ControlController : public drogon::HttpController<ControlController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ControlController::write, "/api/v1/control/write", drogon::Post);
    ADD_METHOD_TO(ControlController::getCommand, "/api/v1/control/commands/{1}", drogon::Get);
    ADD_METHOD_TO(ControlController::queueDepth, "/api/v1/control/queue-depth", drogon::Get);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> write(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> getCommand(drogon::HttpRequestPtr req,
                                                    int64_t commandId);
    drogon::Task<drogon::HttpResponsePtr> queueDepth(drogon::HttpRequestPtr req);
};

}  // namespace ibmcp