#include "controller/system_controller.h"

#include <chrono>
#include <string>

#include "core/result.h"
#include "db/redis.h"

using namespace drogon;

namespace ibmcp {

Task<HttpResponsePtr> SystemController::ping(HttpRequestPtr) {
    Json::Value data(Json::objectValue);
    data["status"] = "ok";
    data["time"] = static_cast<Json::Int64>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    co_return result::ok(data);
}

namespace {
std::string redisResultToString(const nosql::RedisResult &r) {
    switch (r.type()) {
        case nosql::RedisResultType::kString:
        case nosql::RedisResultType::kStatus:
            return r.asString();
        case nosql::RedisResultType::kInteger:
            return std::to_string(r.asInteger());
        case nosql::RedisResultType::kNil:
            return "(nil)";
        case nosql::RedisResultType::kError:
            return "(error)";
        case nosql::RedisResultType::kArray:
            return "(array)";
    }
    return "(unknown)";
}
}  // namespace

Task<HttpResponsePtr> SystemController::redisPing(HttpRequestPtr) {
    Json::Value data(Json::objectValue);
    data["redis"] = "unknown";
    try {
        auto client = redis::client();
        if (!client) {
            data["redis"] = "unavailable";
            data["error"] =
                "redis client is null (check redis_clients in config.json)";
            co_return result::ok(data);
        }
        auto pong = co_await client->execCommandCoro("PING");
        data["ping"] = redisResultToString(pong);

        // 证明读 + 写能力：写入健康标记后读回，并取 DBSIZE 作为证据。
        const char *key = "ibmcp:healthcheck";
        co_await client->execCommandCoro("SET %s %s", key, "1");
        auto got = co_await client->execCommandCoro("GET %s", key);
        data["get"] = redisResultToString(got);
        auto dbsize = co_await client->execCommandCoro("DBSIZE");
        data["dbsize"] = redisResultToString(dbsize);
        data["redis"] = "ok";
    } catch (const std::exception &e) {
        data["redis"] = "error";
        data["error"] = e.what();
    }
    co_return result::ok(data);
}

}  // namespace ibmcp