#pragma once

#include <drogon/nosql/RedisClient.h>
#include <drogon/utils/coroutine.h>
#include <json/json.h>

#include <optional>
#include <string>

namespace ibmcp::cache {

// 所有缓存 key 统一此前缀，便于运维检索与清理。
constexpr const char *kPrefix = "cache:";

// 从 Redis 取 JSON 缓存。未命中或 Redis 异常返回 std::nullopt（fail-open：调用方回源）。
drogon::Task<std::optional<Json::Value>> getJson(const std::string &key);

// 写入 JSON 缓存（ttlSec 秒）。Redis 异常仅记日志，不影响业务流程。
drogon::Task<void> setJson(const std::string &key, const Json::Value &value, int ttlSec);

// 删除缓存 key（写时失效）。异常仅记日志。
drogon::Task<void> invalidate(const std::string &key);

// 按 glob pattern 批量删除（SCAN + DEL），用于带参数的列表变体失效。
drogon::Task<void> invalidateMatch(const std::string &pattern);

// 命中即返回缓存；未命中/异常时执行 loader 回源并回填缓存，返回 loader 结果。
// Loader: 可调用对象，签名返回 drogon::Task<Json::Value>。
template <class Loader>
drogon::Task<Json::Value> getOrSet(const std::string &key, int ttlSec, Loader &&loader) {
    auto cached = co_await getJson(key);
    if (cached) co_return *cached;
    Json::Value value = co_await loader();
    co_await setJson(key, value, ttlSec);
    co_return value;
}

}  // namespace ibmcp::cache
