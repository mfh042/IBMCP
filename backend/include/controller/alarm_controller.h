#pragma once

#include <drogon/HttpController.h>

namespace ibmcp {

class AlarmController : public drogon::HttpController<AlarmController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AlarmController::list, "/api/v1/alarms/list", drogon::Get);
    ADD_METHOD_TO(AlarmController::stats, "/api/v1/alarms/stats", drogon::Get);
    ADD_METHOD_TO(AlarmController::get, "/api/v1/alarms/{1}", drogon::Get);
    ADD_METHOD_TO(AlarmController::ack, "/api/v1/alarms/{1}/ack", drogon::Put);
    ADD_METHOD_TO(AlarmController::recover, "/api/v1/alarms/{1}/recover", drogon::Put);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> list(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> stats(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> get(drogon::HttpRequestPtr req, int64_t alarmId);
    drogon::Task<drogon::HttpResponsePtr> ack(drogon::HttpRequestPtr req, int64_t alarmId);
    drogon::Task<drogon::HttpResponsePtr> recover(drogon::HttpRequestPtr req, int64_t alarmId);
};

}  // namespace ibmcp