#include "core/config.h"

#include "util/json_util.h"

#include <algorithm>

namespace ibmcp::config {

namespace {
const Json::Value &safeCustom() {
    static Json::Value empty;
    const auto &cfg = drogon::app().getCustomConfig();
    return cfg.isNull() ? empty : cfg;
}
}  // namespace

const Json::Value &custom() { return safeCustom(); }

std::string appName() {
    const auto &c = safeCustom();
    return c.get("app_name", "IBMCP").asString();
}

std::string appSecretKey() {
    return safeCustom().get("app_secret_key", "dev-secret-change-me").asString();
}

int accessTokenExpireMinutes() {
    return safeCustom().get("access_token_expire_minutes", 1440).asInt();
}

std::vector<std::string> corsOrigins() {
    std::vector<std::string> out;
    const auto &arr = safeCustom()["cors_origins"];
    if (arr.isArray()) {
        for (const auto &v : arr) out.push_back(v.asString());
    }
    if (out.empty()) out.emplace_back("http://localhost:5173");
    return out;
}

std::string pgDsn() {
    return safeCustom().get("pg_dsn", "postgresql://ibmcp:ibmcp@127.0.0.1:5432/ibmcp").asString();
}

std::string migrationsDir() {
    return safeCustom().get("migrations_dir", "./migrations").asString();
}

int samplePumpIntervalMs() {
    return safeCustom().get("sample_pump_interval_ms", 200).asInt();
}

int samplePumpBatchSize() {
    return safeCustom().get("sample_pump_batch_size", 500).asInt();
}

nlohmann::json collectorCfg() {
    const auto &v = safeCustom()["collector_cfg"];
    if (v.isNull()) return nlohmann::json::object();
    return util::toNlohmann(v);
}

const Json::Value &publicPaths() {
    static Json::Value empty(Json::arrayValue);
    const auto &v = safeCustom()["public_paths"];
    return v.isArray() ? v : empty;
}

bool isPublicPath(const std::string &path) {
    const auto &arr = publicPaths();
    for (const auto &v : arr) {
        if (v.asString() == path) return true;
    }
    return false;
}

}  // namespace ibmcp::config