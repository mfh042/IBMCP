#pragma once

#include <drogon/HttpController.h>

namespace ibmcp {

class StaticDictController : public drogon::HttpController<StaticDictController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(StaticDictController::bootstrap, "/api/v1/static/bootstrap", drogon::Get);
    ADD_METHOD_TO(StaticDictController::all, "/api/v1/static/{1}/all", drogon::Get);
    ADD_METHOD_TO(StaticDictController::byCode, "/api/v1/static/{1}/code/{2}", drogon::Get);
    ADD_METHOD_TO(StaticDictController::byId, "/api/v1/static/{1}/{2}", drogon::Get);
    ADD_METHOD_TO(StaticDictController::page, "/api/v1/static/{1}", drogon::Get);
    ADD_METHOD_TO(StaticDictController::create, "/api/v1/static/{1}", drogon::Post);
    ADD_METHOD_TO(StaticDictController::update, "/api/v1/static/{1}/{2}", drogon::Put);
    ADD_METHOD_TO(StaticDictController::remove, "/api/v1/static/{1}/{2}", drogon::Delete);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> bootstrap(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> page(drogon::HttpRequestPtr req,
                                               std::string resource);
    drogon::Task<drogon::HttpResponsePtr> all(drogon::HttpRequestPtr req,
                                              std::string resource);
    drogon::Task<drogon::HttpResponsePtr> byCode(drogon::HttpRequestPtr req,
                                                 std::string resource, std::string code);
    drogon::Task<drogon::HttpResponsePtr> byId(drogon::HttpRequestPtr req,
                                               std::string resource, int64_t rowId);
    drogon::Task<drogon::HttpResponsePtr> create(drogon::HttpRequestPtr req,
                                                 std::string resource);
    drogon::Task<drogon::HttpResponsePtr> update(drogon::HttpRequestPtr req,
                                                 std::string resource, int64_t rowId);
    drogon::Task<drogon::HttpResponsePtr> remove(drogon::HttpRequestPtr req,
                                                 std::string resource, int64_t rowId);
};

}  // namespace ibmcp