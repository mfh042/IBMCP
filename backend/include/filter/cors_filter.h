#pragma once

#include <drogon/HttpFilter.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <string>

namespace ibmcp::filter {

std::string matchOrigin(const std::string &origin);

void applyCorsHeaders(const drogon::HttpRequestPtr &req,
                      const drogon::HttpResponsePtr &resp);

class CorsFilter : public drogon::HttpFilter<CorsFilter> {
  public:
    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcc,
                  drogon::FilterChainCallback &&fbp) override;
};

}  // namespace ibmcp::filter