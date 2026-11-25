#pragma once

#include <cstdint>

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <nlohmann/json.hpp>

#include <memory>

#include "collector/collector_bridge.h"
#include "collector/plan_compiler.h"

namespace ibmcp {

class CollectorOrchestrator {
  public:
    CollectorOrchestrator(drogon::orm::DbClientPtr db, CollectorBridge &collector);

    drogon::Task<nlohmann::json> bootstrap();
    drogon::Task<nlohmann::json> activateConfig(int64_t configId);
    drogon::Task<nlohmann::json> deactivateConfig(int64_t configId);
    drogon::Task<nlohmann::json> compileOnly(int64_t configId);
    drogon::Task<nlohmann::json> recompileAll();

  private:
    drogon::Task<int64_t> nextRevision();
    drogon::Task<nlohmann::json> compileFull(int64_t revision);
    drogon::Task<void> setStatus(int64_t configId, int16_t status);
    drogon::Task<void> apply(const nlohmann::json &plan, int64_t configId);
    drogon::Task<void> syncRuntimeSnapshot(const nlohmann::json &plan);

    drogon::orm::DbClientPtr db_;
    CollectorBridge &collector_;
    PlanCompiler compiler_;
};

}  // namespace ibmcp