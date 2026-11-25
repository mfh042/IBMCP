#pragma once

#include <drogon/nosql/RedisClient.h>
#include <drogon/utils/coroutine.h>

#include <functional>
#include <string>

namespace ibmcp::auth {

// Token 黑名单 key 前缀：auth:blacklist:<jti>
constexpr const char *kBlacklistPrefix = "auth:blacklist:";

// 异步查询 jti 是否在黑名单；命中 cb(true)，未命中/Redis 异常 cb(false)（fail-open 放行）。
// 供同步上下文（如 main.cpp 全局 advice）调用。
void isBlacklisted(const std::string &jti, std::function<void(bool)> cb);

// 协程版（供登出等协程上下文可选使用）。
drogon::Task<bool> isBlacklistedCoro(const std::string &jti);

// 将 jti 加入黑名单，TTL = ttlSec 秒；ttl<=0 不写（token 已自然过期）。
// Redis 异常仅记日志，不抛。
drogon::Task<void> blacklist(const std::string &jti, int64_t ttlSec);

}  // namespace ibmcp::auth
