#include "db/redis.h"

#include "core/config.h"

namespace ibmcp::redis {

drogon::nosql::RedisClientPtr client() {
    return drogon::app().getRedisClient(config::kDefaultRedis);
}

}  // namespace ibmcp::redis
