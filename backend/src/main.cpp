#include <drogon/drogon.h>

#include "core/app_context.h"
#include "core/auth_redis.h"
#include "core/config.h"
#include "core/errors.h"
#include "core/jwt_util.h"
#include "core/result.h"
#include "collector/orchestrator.h"
#include "db/migrator.h"
#include "filter/cors_filter.h"
#include "ws/ws_hub.h"

#include <memory>

using namespace drogon;

int main() {
    app().loadConfigFile("config.json");

    ibmcp::db::Migrator(ibmcp::config::migrationsDir(), ibmcp::config::pgDsn())
        .migrate();
    LOG_INFO << "db migrations applied";

    // CORS headers are only emitted when the request Origin matches an entry in
    // config.json cors_origins (echo the exact origin instead of "*"), so the
    // production origin allow-list actually takes effect.
    app().registerPostHandlingAdvice([](const HttpRequestPtr &req,
                                        const HttpResponsePtr &resp) {
        ibmcp::filter::applyCorsHeaders(req, resp);
    });

    // Intercept CORS preflight (OPTIONS) before routing/auth so browsers receive
    // a 204 with the proper CORS headers even on bearer-protected paths (the
    // preflight request carries no Authorization header).
    app().registerPreRoutingAdvice(
        [](const HttpRequestPtr &req, AdviceCallback &&adviceCb,
           AdviceChainCallback &&chainCb) {
            if (req->method() == Options) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k204NoContent);
                ibmcp::filter::applyCorsHeaders(req, resp);
                adviceCb(resp);
                return;
            }
            chainCb();
        });

    // Global auth. Drogon 1.9.5 removed registerPostRoutingFilter; enforce the
    // bearer-token check via a post-routing advice (runs after routing, before
    // the handler). Public paths and WebSocket endpoints bypass it; the verified
    // JWT payload is stored on the request as the "user" attribute, which the
    // in-controller auth::requireAuth / ctrl helpers then read.
    app().registerPostRoutingAdvice(
        [](const HttpRequestPtr &req, AdviceCallback &&adviceCb,
           AdviceChainCallback &&chainCb) {
            const std::string &path = req->path();
            if (ibmcp::config::isPublicPath(path) || path == "/ws/push" ||
                path == "/api/v1/ws/collect") {
                chainCb();
                return;
            }
            std::string auth = req->getHeader("Authorization");
            const std::string prefix = "Bearer ";
            if (auth.size() <= prefix.size() ||
                auth.compare(0, prefix.size(), prefix) != 0) {
                adviceCb(ibmcp::result::error(ibmcp::err::UNAUTHORIZED,
                                              "missing bearer token"));
                return;
            }
            std::string token = auth.substr(prefix.size());
            auto payload = ibmcp::jwt::verifyToken(token);
            if (!payload) {
                adviceCb(ibmcp::result::error(ibmcp::err::UNAUTHORIZED,
                                              "invalid or expired token"));
                return;
            }
            std::string jti = payload->isMember("jti") && !(*payload)["jti"].isNull()
                                  ? (*payload)["jti"].asString()
                                  : "";
            if (jti.empty()) {
                // 存量旧 token 无 jti，放行至自然过期（向后兼容）。
                req->getAttributes()->insert("user", *payload);
                chainCb();
                return;
            }
            auto payloadPtr = std::make_shared<Json::Value>(*payload);
            ibmcp::auth::isBlacklisted(
                jti,
                [req, adviceCb = std::move(adviceCb), chainCb = std::move(chainCb),
                 payloadPtr](bool blacklisted) mutable {
                    if (blacklisted) {
                        LOG_INFO << "token revoked (jti blacklisted)";
                        adviceCb(ibmcp::result::error(ibmcp::err::UNAUTHORIZED,
                                                      "token revoked"));
                        return;
                    }
                    req->getAttributes()->insert("user", *payloadPtr);
                    chainCb();
                });
        });

    app().setExceptionHandler([](const std::exception &e, HttpRequestPtr,
                                 std::function<void(const HttpResponsePtr &)> &&cb) {
        auto *be = dynamic_cast<const ibmcp::BizError *>(&e);
        if (be) {
            cb(ibmcp::result::error(be->code(), be->message(), be->data()));
        } else {
            LOG_ERROR << "unhandled exception: " << e.what();
            cb(ibmcp::result::error(ibmcp::err::INTERNAL, e.what()));
        }
    });

    ibmcp::AppContext &ctx = ibmcp::AppContext::instance();
    app().registerBeginningAdvice([&ctx]() {
        ctx.init();
        ibmcp::ws::WsHub::instance().init();
        [&ctx]() -> drogon::Task<> {
            try {
                co_await ctx.orchestrator().bootstrap();
                LOG_INFO << "collector bootstrap completed";
            } catch (const std::exception &e) {
                LOG_ERROR << "collector bootstrap failed: " << e.what();
            }
        }();
    });

    LOG_INFO << "IBMCP server starting on " << app().getListeners().size() << " listener(s)";
    app().run();
    return 0;
}