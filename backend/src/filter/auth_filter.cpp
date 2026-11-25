#include "filter/auth_filter.h"

#include <drogon/drogon.h>

#include "core/auth_redis.h"
#include "core/config.h"
#include "core/errors.h"
#include "core/jwt_util.h"
#include "core/result.h"

#include <memory>

namespace ibmcp::filter {

void AuthFilter::doFilter(const drogon::HttpRequestPtr &req,
                          drogon::FilterCallback &&fcc,
                          drogon::FilterChainCallback &&fbp) {
    const std::string &path = req->path();
    if (config::isPublicPath(path)) {
        fbp();
        return;
    }
    if (path == "/ws/push" || path == "/api/v1/ws/collect") {
        fbp();
        return;
    }

    std::string auth = req->getHeader("Authorization");
    const std::string prefix = "Bearer ";
    if (auth.size() <= prefix.size() || auth.compare(0, prefix.size(), prefix) != 0) {
        fcc(result::error(err::UNAUTHORIZED, "missing bearer token"));
        return;
    }
    std::string token = auth.substr(prefix.size());
    auto payload = jwt::verifyToken(token);
    if (!payload) {
        fcc(result::error(err::UNAUTHORIZED, "invalid or expired token"));
        return;
    }
    std::string jti = payload->isMember("jti") && !(*payload)["jti"].isNull()
                          ? (*payload)["jti"].asString()
                          : "";
    if (jti.empty()) {
        req->getAttributes()->insert("user", *payload);
        fbp();
        return;
    }
    auto payloadPtr = std::make_shared<Json::Value>(*payload);
    auth::isBlacklisted(jti, [req, fcc = std::move(fcc), fbp = std::move(fbp),
                              payloadPtr](bool blacklisted) mutable {
        if (blacklisted) {
            LOG_INFO << "token revoked (jti blacklisted)";
            fcc(result::error(err::UNAUTHORIZED, "token revoked"));
            return;
        }
        req->getAttributes()->insert("user", *payloadPtr);
        fbp();
    });
}

}  // namespace ibmcp::filter