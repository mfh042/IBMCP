#pragma once

#include <drogon/nosql/RedisClient.h>

namespace ibmcp::redis {

// 取默认 Redis 客户端（对应 config.json 中 redis_clients[name="default"]）。
// 必须在 Drogon framework 启动后调用；若未配置则返回 nullptr。
drogon::nosql::RedisClientPtr client();

}  // namespace ibmcp::redis
