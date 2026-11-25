#include "filter/cors_filter.h"

#include <string>
#include <vector>

#include "core/config.h"

namespace ibmcp::filter {

namespace {

constexpr const char *kAllowHeaders = "Content-Type, Authorization";
constexpr const char *kAllowMethods = "GET, POST, PUT, DELETE, OPTIONS";
constexpr const char *kMaxAge = "86400";

}  // namespace

std::string matchOrigin(const std::string &origin) {
    if (origin.empty()) return "";
    const std::vector<std::string> origins = config::corsOrigins();
    for (const auto &o : origins) {
        if (o == origin) return origin;
    }
    return "";
}

void applyCorsHeaders(const drogon::HttpRequestPtr &req,
                      const drogon::HttpResponsePtr &resp) {
    std::string origin = req ? matchOrigin(req->getHeader("Origin")) : std::string();
    if (origin.empty()) return;
    resp->addHeader("Access-Control-Allow-Origin", origin);
    resp->addHeader("Vary", "Origin");
    resp->addHeader("Access-Control-Allow-Headers", kAllowHeaders);
    resp->addHeader("Access-Control-Allow-Methods", kAllowMethods);
    resp->addHeader("Access-Control-Max-Age", kMaxAge);
}

void CorsFilter::doFilter(const drogon::HttpRequestPtr &req,
                          drogon::FilterCallback &&fcc,
                          drogon::FilterChainCallback &&fbp) {
    if (req->method() == drogon::Options) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k204NoContent);
        applyCorsHeaders(req, resp);
        fcc(resp);
        return;
    }
    fbp();
}

}  // namespace ibmcp::filter