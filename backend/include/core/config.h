#pragma once

#include <drogon/drogon.h>
#include <json/json.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace ibmcp::config {

const Json::Value &custom();

std::string appName();
std::string appSecretKey();
int accessTokenExpireMinutes();
std::vector<std::string> corsOrigins();
std::string pgDsn();
std::string migrationsDir();
int samplePumpIntervalMs();
int samplePumpBatchSize();
nlohmann::json collectorCfg();
const Json::Value &publicPaths();
bool isPublicPath(const std::string &path);

constexpr const char *kDefaultDb = "default";
constexpr const char *kWriteDb = "write";
constexpr const char *kDefaultRedis = "default";

}  // namespace ibmcp::config