#pragma once

#include <cstdint>

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <nlohmann/json.hpp>

namespace ibmcp {

std::string planHash(const nlohmann::json &plan);

class PlanCompiler {
  public:
    explicit PlanCompiler(drogon::orm::DbClientPtr db) : db_(std::move(db)) {}

    drogon::Task<nlohmann::json> compileForConfig(int64_t configId, int64_t revision);
    drogon::Task<nlohmann::json> compileFull(int64_t revision);

  private:
    nlohmann::json buildPlan(const drogon::orm::Result &rows, int64_t revision);
    drogon::orm::DbClientPtr db_;
};

}  // namespace ibmcp