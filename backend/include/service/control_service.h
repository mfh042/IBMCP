#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <json/json.h>

#include <string>

namespace ibmcp::service {

class ControlService {
  public:
    explicit ControlService(drogon::orm::DbClientPtr db) : db_(std::move(db)) {}

    drogon::Task<Json::Value> write(const std::string &deviceCode, int pointId,
                                    const std::string &value,
                                    const std::string &pointName = "", bool wait = true,
                                    const std::string &issuedBy = "", double timeoutSec = 3.0);
    drogon::Task<Json::Value> getCommand(int64_t commandId);
    Json::Value queueDepth();

  private:
    static void validateValue(const std::string &value, const std::string &dataType);
    drogon::orm::DbClientPtr db_;
};

}  // namespace ibmcp::service