#include "controller/data_controller.h"

#include <algorithm>

#include "controller/common.h"
#include "core/auth.h"
#include "core/cache.h"
#include "core/result.h"
#include "db/db.h"
#include "service/query_service.h"

using namespace drogon;

namespace ibmcp {

Task<HttpResponsePtr> DataController::history(HttpRequestPtr req) {
    auth::requireAuth(req);
    int64_t pointId = ctrl::getInt64Param(req, "point_id", 0);
    int64_t startTs = ctrl::getInt64Param(req, "start_ts", 0);
    int64_t endTs = ctrl::getInt64Param(req, "end_ts", 0);
    std::string bucket = ctrl::getStrParam(req, "bucket", "raw");
    std::string func = ctrl::getStrParam(req, "func", "avg");
    int64_t limit = ctrl::getIntParam(req, "limit", 50000);
    limit = std::clamp<int64_t>(limit, 1, 200000);
    service::QueryService svc(db::ro());
    co_return result::ok(co_await svc.history(pointId, startTs, endTs, bucket, func, limit));
}

Task<HttpResponsePtr> DataController::latest(HttpRequestPtr req) {
    auth::requireAuth(req);
    std::string deviceCode = ctrl::getStrParam(req, "device_code");
    std::string pointIds = ctrl::getStrParam(req, "point_ids");
    std::string key = std::string(cache::kPrefix) + "data:latest:" + deviceCode + ":" +
                      (pointIds.empty() ? "_" : pointIds);
    auto data = co_await cache::getOrSet(key, 5, [&]() -> Task<Json::Value> {
        service::QueryService svc(db::ro());
        co_return co_await svc.latest(deviceCode, pointIds);
    });
    co_return result::ok(data);
}

}  // namespace ibmcp