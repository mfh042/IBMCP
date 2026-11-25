#pragma once

#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <string>

namespace ibmcp::result {

inline drogon::HttpResponsePtr make(int code, const std::string &message,
                                    const Json::Value &data = Json::Value()) {
    Json::Value root;
    root["code"] = code;
    root["message"] = message;
    root["data"] = data;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(root);
    resp->setStatusCode(drogon::k200OK);
    return resp;
}

inline drogon::HttpResponsePtr ok(const Json::Value &data = Json::Value()) {
    return make(0, "ok", data);
}

inline drogon::HttpResponsePtr error(int code, const std::string &message,
                                     const Json::Value &data = Json::Value()) {
    return make(code, message, data);
}

inline drogon::HttpResponsePtr paginated(int64_t total, const Json::Value &items) {
    Json::Value data(Json::objectValue);
    data["items"] = items;
    data["total"] = static_cast<Json::Int64>(total);
    return ok(data);
}

}  // namespace ibmcp::result