#include "controller/static_dict_controller.h"

#include <algorithm>

#include "controller/common.h"
#include "core/auth.h"
#include "core/cache.h"
#include "core/result.h"
#include "service/static_dict_service.h"

using namespace drogon;

namespace ibmcp {

namespace {

constexpr int kDictTtl = 600;

std::string allKey(const std::string &resource) {
    return std::string(cache::kPrefix) + "static_dict:" + resource + ":all";
}

}  // namespace

Task<HttpResponsePtr> StaticDictController::bootstrap(HttpRequestPtr req) {
    auth::requireAuth(req);
    std::string key = std::string(cache::kPrefix) + "static_dict:bootstrap";
    auto data = co_await cache::getOrSet(key, kDictTtl, [&]() -> Task<Json::Value> {
        co_return co_await service::StaticDictService::instance().bootstrap();
    });
    co_return result::ok(data);
}

Task<HttpResponsePtr> StaticDictController::page(HttpRequestPtr req, std::string resource) {
    auth::requireAuth(req);
    int page = std::max(1, ctrl::getIntParam(req, "page", 1));
    int size = std::clamp(ctrl::getIntParam(req, "size", 20), 1, 10000);
    std::string keyword = ctrl::getStrParam(req, "keyword");
    std::map<std::string, std::string> filters;
    for (const auto &[k, v] : req->getParameters()) {
        if (k == "page" || k == "size" || k == "keyword") continue;
        filters[k] = v;
    }
    std::string filterKey;
    for (const auto &[k, v] : filters) filterKey += k + "=" + v + "&";
    std::string key = std::string(cache::kPrefix) + "static_dict:" + resource + ":page:" +
                      std::to_string(page) + ":" + std::to_string(size) + ":" + keyword + ":" +
                      filterKey;
    auto data = co_await cache::getOrSet(key, kDictTtl, [&]() -> Task<Json::Value> {
        co_return co_await service::StaticDictService::instance().page(resource, page, size,
                                                                       keyword, filters);
    });
    co_return result::ok(data);
}

Task<HttpResponsePtr> StaticDictController::all(HttpRequestPtr req, std::string resource) {
    auth::requireAuth(req);
    auto data = co_await cache::getOrSet(allKey(resource), kDictTtl,
                                         [&]() -> Task<Json::Value> {
                                             co_return co_await service::StaticDictService::instance()
                                                 .all(resource);
                                         });
    co_return result::ok(data);
}

Task<HttpResponsePtr> StaticDictController::byCode(HttpRequestPtr req, std::string resource,
                                                   std::string code) {
    auth::requireAuth(req);
    std::string key = std::string(cache::kPrefix) + "static_dict:" + resource + ":code:" + code;
    auto data = co_await cache::getOrSet(key, kDictTtl, [&]() -> Task<Json::Value> {
        co_return co_await service::StaticDictService::instance().byCode(resource, code);
    });
    co_return result::ok(data);
}

Task<HttpResponsePtr> StaticDictController::byId(HttpRequestPtr req, std::string resource,
                                                 int64_t rowId) {
    auth::requireAuth(req);
    std::string key =
        std::string(cache::kPrefix) + "static_dict:" + resource + ":" + std::to_string(rowId);
    auto data = co_await cache::getOrSet(key, kDictTtl, [&]() -> Task<Json::Value> {
        co_return co_await service::StaticDictService::instance().byId(resource, rowId);
    });
    co_return result::ok(data);
}

Task<HttpResponsePtr> StaticDictController::create(HttpRequestPtr req,
                                                   std::string resource) {
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    auto j = req->getJsonObject();
    if (!j) throw BizError(err::VALIDATION_FAILED, "request body must be an object");
    auto data = co_await service::StaticDictService::instance().create(resource, *j);
    co_await cache::invalidate(std::string(cache::kPrefix) + "static_dict:bootstrap");
    co_await cache::invalidate(allKey(resource));
    co_return result::ok(data);
}

Task<HttpResponsePtr> StaticDictController::update(HttpRequestPtr req, std::string resource,
                                                   int64_t rowId) {
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    auto j = req->getJsonObject();
    if (!j) throw BizError(err::VALIDATION_FAILED, "request body must be an object");
    auto data = co_await service::StaticDictService::instance().update(resource, rowId, *j);
    co_await cache::invalidate(std::string(cache::kPrefix) + "static_dict:bootstrap");
    co_await cache::invalidate(allKey(resource));
    co_return result::ok(data);
}

Task<HttpResponsePtr> StaticDictController::remove(HttpRequestPtr req, std::string resource,
                                                   int64_t rowId) {
    ctrl::requireAnyRole(req, {"engineer", "admin"});
    auto data = co_await service::StaticDictService::instance().remove(resource, rowId);
    co_await cache::invalidate(std::string(cache::kPrefix) + "static_dict:bootstrap");
    co_await cache::invalidate(allKey(resource));
    co_return result::ok(data);
}

}  // namespace ibmcp
