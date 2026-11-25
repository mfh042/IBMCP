#pragma once

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

namespace ibmcp::auth {

struct CurrentUser {
    std::string sub;
    std::string username;
    std::vector<std::string> roles;
};

std::optional<Json::Value> currentUserPayload(const drogon::HttpRequestPtr &req);

std::optional<CurrentUser> currentUser(const drogon::HttpRequestPtr &req);

void requireAuth(const drogon::HttpRequestPtr &req);

bool hasRole(const drogon::HttpRequestPtr &req, const std::string &role);

void requireRole(const drogon::HttpRequestPtr &req, const std::string &role);

}  // namespace ibmcp::auth