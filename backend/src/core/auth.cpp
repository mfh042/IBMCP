#include "core/auth.h"

#include "core/errors.h"

namespace ibmcp::auth {

std::optional<Json::Value> currentUserPayload(const drogon::HttpRequestPtr &req) {
    auto attrs = req->getAttributes();
    if (!attrs || !attrs->find("user")) return std::nullopt;
    return attrs->get<Json::Value>("user");
}

std::optional<CurrentUser> currentUser(const drogon::HttpRequestPtr &req) {
    auto pl = currentUserPayload(req);
    if (!pl) return std::nullopt;
    CurrentUser u;
    u.sub = (*pl).get("sub", "").asString();
    u.username = (*pl).get("username", "").asString();
    const auto &roles = (*pl)["roles"];
    if (roles.isArray()) {
        for (const auto &r : roles) u.roles.push_back(r.asString());
    }
    return u;
}

void requireAuth(const drogon::HttpRequestPtr &req) {
    if (!currentUserPayload(req)) {
        throw BizError(err::UNAUTHORIZED, "unauthorized");
    }
}

bool hasRole(const drogon::HttpRequestPtr &req, const std::string &role) {
    auto u = currentUser(req);
    if (!u) return false;
    for (const auto &r : u->roles) {
        if (r == role) return true;
    }
    return false;
}

void requireRole(const drogon::HttpRequestPtr &req, const std::string &role) {
    requireAuth(req);
    if (!hasRole(req, role)) {
        throw BizError(err::FORBIDDEN, "forbidden");
    }
}

}  // namespace ibmcp::auth