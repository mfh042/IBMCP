#pragma once

#include <drogon/HttpController.h>

namespace ibmcp {

// Controls the Node-RED editor container (ibmcp-nodered).
//   GET  /api/v1/node-red/status   -> any authenticated user
//   POST /api/v1/node-red/start    -> admin
//   POST /api/v1/node-red/stop     -> admin
//   POST /api/v1/node-red/restart  -> admin
class NodeRedController : public drogon::HttpController<NodeRedController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(NodeRedController::status, "/api/v1/node-red/status", drogon::Get);
    ADD_METHOD_TO(NodeRedController::start, "/api/v1/node-red/start", drogon::Post);
    ADD_METHOD_TO(NodeRedController::stop, "/api/v1/node-red/stop", drogon::Post);
    ADD_METHOD_TO(NodeRedController::restart, "/api/v1/node-red/restart", drogon::Post);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> status(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> start(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> stop(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> restart(drogon::HttpRequestPtr req);
};

}  // namespace ibmcp
