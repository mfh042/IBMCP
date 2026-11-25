#pragma once

#include <drogon/HttpFilter.h>

namespace ibmcp::filter {

class AuthFilter : public drogon::HttpFilter<AuthFilter> {
  public:
    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcc,
                  drogon::FilterChainCallback &&fbp) override;
};

}  // namespace ibmcp::filter