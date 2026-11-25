#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <json/json.h>
#include <json/writer.h>

#include <string>

#include "db/db.h"

namespace ibmcp::service {

inline drogon::Task<void> writeConfigChange(const std::string &level, int64_t targetId,
                                            const std::string &action,
                                            const std::string &operator_,
                                            const Json::Value &detail, int64_t revAfter = 0,
                                            int64_t revBefore = 0) {
    try {
        Json::Value d = detail.isNull() ? Json::Value(Json::objectValue) : detail;
        Json::StreamWriterBuilder b;
        b["indentation"] = "";
        std::string detailStr = Json::writeString(b, d);
        co_await db::rw()->execSqlCoro(
            "INSERT INTO audit_config_change (target_level, target_id, action, "
            "plan_revision_before, plan_revision_after, operator, detail) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7)",
            level, targetId, action, revBefore, revAfter, operator_, detailStr);
    } catch (...) {
    }
    co_return;
}

}  // namespace ibmcp::service