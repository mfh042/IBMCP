#pragma once

#include <drogon/HttpController.h>

namespace ibmcp {

class DataController : public drogon::HttpController<DataController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(DataController::history, "/api/v1/data/history", drogon::Get);
    ADD_METHOD_TO(DataController::latest, "/api/v1/data/latest", drogon::Get);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> history(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> latest(drogon::HttpRequestPtr req);
};

}  // namespace ibmcp