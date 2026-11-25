#include "core/cache.h"

#include <json/reader.h>
#include <json/writer.h>

#include <drogon/drogon.h>

#include "db/redis.h"

namespace ibmcp::cache {

namespace {

std::string compact(const Json::Value &value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

// 解析失败返回 std::nullopt，让调用方回源（fail-open）。
std::optional<Json::Value> parseJson(const std::string &raw) {
    Json::Value value;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(raw.data(), raw.data() + raw.size(), &value, &errs)) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

drogon::Task<std::optional<Json::Value>> getJson(const std::string &key) {
    auto client = redis::client();
    if (!client) co_return std::nullopt;
    try {
        auto result = co_await client->execCommandCoro("GET %s", key.c_str());
        if (result.isNil()) co_return std::nullopt;
        co_return parseJson(result.asString());
    } catch (const std::exception &e) {
        LOG_ERROR << "cache get failed key=" << key << " err=" << e.what();
        co_return std::nullopt;
    }
}

drogon::Task<void> setJson(const std::string &key, const Json::Value &value, int ttlSec) {
    auto client = redis::client();
    if (!client) co_return;
    try {
        std::string raw = compact(value);
        co_await client->execCommandCoro("SET %s %s EX %d", key.c_str(),
                                         raw.c_str(), ttlSec);
    } catch (const std::exception &e) {
        LOG_ERROR << "cache set failed key=" << key << " err=" << e.what();
    }
}

drogon::Task<void> invalidate(const std::string &key) {
    auto client = redis::client();
    if (!client) co_return;
    try {
        co_await client->execCommandCoro("DEL %s", key.c_str());
    } catch (const std::exception &e) {
        LOG_ERROR << "cache invalidate failed key=" << key << " err=" << e.what();
    }
}

drogon::Task<void> invalidateMatch(const std::string &pattern) {
    auto client = redis::client();
    if (!client) co_return;
    try {
        std::string cursor = "0";
        do {
            auto r = co_await client->execCommandCoro("SCAN %s MATCH %s COUNT 200",
                                                      cursor.c_str(), pattern.c_str());
            if (r.type() != drogon::nosql::RedisResultType::kArray) break;
            auto arr = r.asArray();
            if (arr.size() < 2) break;
            cursor = arr[0].asString();
            auto keys = arr[1].asArray();
            for (const auto &k : keys) {
                co_await client->execCommandCoro("DEL %s", k.asString().c_str());
            }
        } while (cursor != "0");
    } catch (const std::exception &e) {
        LOG_ERROR << "cache invalidateMatch failed pattern=" << pattern
                  << " err=" << e.what();
    }
}

}  // namespace ibmcp::cache
