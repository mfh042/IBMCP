#include "core/auth_redis.h"

#include <drogon/drogon.h>

#include "db/redis.h"

namespace ibmcp::auth {

namespace {

std::string blacklistKey(const std::string &jti) {
    return std::string(kBlacklistPrefix) + jti;
}

}  // namespace

void isBlacklisted(const std::string &jti, std::function<void(bool)> cb) {
    if (jti.empty()) {
        cb(false);
        return;
    }
    auto client = ibmcp::redis::client();
    if (!client) {
        LOG_ERROR << "auth blacklist check skipped: redis client null (fail-open)";
        cb(false);
        return;
    }
    std::string key = blacklistKey(jti);
    client->execCommandAsync(
        [cb](const drogon::nosql::RedisResult &r) {
            try {
                if (r.type() == drogon::nosql::RedisResultType::kInteger) {
                    cb(r.asInteger() != 0);
                } else {
                    cb(false);
                }
            } catch (const std::exception &e) {
                LOG_ERROR << "auth blacklist check error (fail-open): " << e.what();
                cb(false);
            }
        },
        [cb](const drogon::nosql::RedisException &e) {
            LOG_ERROR << "auth blacklist query failed (fail-open): " << e.what();
            cb(false);
        },
        "EXISTS %s", key.c_str());
}

drogon::Task<bool> isBlacklistedCoro(const std::string &jti) {
    if (jti.empty()) co_return false;
    auto client = ibmcp::redis::client();
    if (!client) {
        LOG_ERROR << "auth blacklist coro check skipped: redis null (fail-open)";
        co_return false;
    }
    try {
        auto r = co_await client->execCommandCoro("EXISTS %s", blacklistKey(jti).c_str());
        if (r.type() == drogon::nosql::RedisResultType::kInteger) {
            co_return r.asInteger() != 0;
        }
        co_return false;
    } catch (const std::exception &e) {
        LOG_ERROR << "auth blacklist coro query failed (fail-open): " << e.what();
        co_return false;
    }
}

drogon::Task<void> blacklist(const std::string &jti, int64_t ttlSec) {
    if (jti.empty() || ttlSec <= 0) co_return;
    auto client = ibmcp::redis::client();
    if (!client) {
        LOG_ERROR << "auth blacklist write skipped: redis client null";
        co_return;
    }
    try {
        std::string key = blacklistKey(jti);
        co_await client->execCommandCoro("SET %s %s EX %d", key.c_str(), "1",
                                         static_cast<int>(ttlSec));
    } catch (const std::exception &e) {
        LOG_ERROR << "auth blacklist write failed: " << e.what();
    }
}

}  // namespace ibmcp::auth
