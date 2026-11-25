#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <json/json.h>

#include <string>

namespace ibmcp::service {

class QueryService {
  public:
    explicit QueryService(drogon::orm::DbClientPtr db) : db_(std::move(db)) {}

    drogon::Task<Json::Value> history(int64_t pointId, int64_t startTs, int64_t endTs,
                                      std::string bucket, std::string func, int64_t limit);
    drogon::Task<Json::Value> latest(const std::string &deviceCode, const std::string &pointIds);

  private:
    drogon::orm::DbClientPtr db_;
};

}  // namespace ibmcp::service