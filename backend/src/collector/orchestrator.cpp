#include "collector/orchestrator.h"

#include "core/collector_error.h"
#include "core/errors.h"

#include <drogon/drogon.h>

#include <ctime>
#include <stdexcept>

namespace ibmcp {

CollectorOrchestrator::CollectorOrchestrator(drogon::orm::DbClientPtr db, CollectorBridge &collector)
    : db_(std::move(db)), collector_(collector), compiler_(db_) {}

drogon::Task<int64_t> CollectorOrchestrator::nextRevision() {
    auto r = co_await db_->execSqlCoro(
        "SELECT COALESCE(MAX(revision), 0) AS rev FROM runtime_plan_revision");
    co_return r[0]["rev"].as<int64_t>() + 1;
}

drogon::Task<nlohmann::json> CollectorOrchestrator::compileFull(int64_t revision) {
    try {
        co_return co_await compiler_.compileFull(revision);
    } catch (const ibmcp::BizError &) {
        throw;
    } catch (const std::exception &e) {
        throw ibmcp::BizError(ibmcp::err::VALIDATION_FAILED,
                              std::string("plan compile failed: ") + e.what());
    }
}

drogon::Task<void> CollectorOrchestrator::setStatus(int64_t configId, int16_t status) {
    co_await db_->execSqlCoro(
        "UPDATE running_equipment_config SET config_status = $2, updated_at = $3 WHERE id = $1",
        configId, status, static_cast<int64_t>(std::time(nullptr)));
}

drogon::Task<nlohmann::json> CollectorOrchestrator::bootstrap() {
    int64_t newRev = co_await nextRevision();
    auto plan = co_await compileFull(newRev);
    LOG_INFO << "CollectorOrchestrator.bootstrap revision=" << newRev
             << " devices=" << plan["devices"].size();
    co_await apply(plan, 0);
    co_return plan;
}

drogon::Task<nlohmann::json> CollectorOrchestrator::activateConfig(int64_t configId) {
    auto exists = co_await db_->execSqlCoro(
        "SELECT id FROM running_equipment_config WHERE id = $1", configId);
    if (exists.size() == 0)
        throw ibmcp::BizError(ibmcp::err::NOT_FOUND, "config not found");

    co_await setStatus(configId, 1);

    auto r = co_await db_->execSqlCoro(
        "SELECT COALESCE(MAX(revision), 0) AS rev, "
        "(SELECT plan_hash FROM runtime_plan_revision ORDER BY revision DESC LIMIT 1) AS last_hash "
        "FROM runtime_plan_revision");
    int64_t newRev = r[0]["rev"].as<int64_t>() + 1;
    std::string lastHash = r[0]["last_hash"].isNull() ? "" : r[0]["last_hash"].as<std::string>();

    struct ActivateResult { nlohmann::json plan; bool skipped; };
    auto inner = [this, configId, newRev, lastHash]() -> drogon::Task<ActivateResult> {
        ActivateResult result;
        try {
            result.plan = co_await compileFull(newRev);
        } catch (...) {
            drogon::sync_wait(setStatus(configId, 0));
            throw;
        }
        result.skipped = (lastHash == result.plan.value("plan_hash", ""));
        if (!result.skipped) {
            try {
                co_await apply(result.plan, configId);
            } catch (...) {
                drogon::sync_wait(setStatus(configId, 0));
                throw;
            }
        }
        co_return result;
    };

    auto result = co_await inner();

    if (result.skipped) {
        nlohmann::json out;
        out["revision"] = newRev - 1;
        out["skipped"] = true;
        out["message"] = "no change";
        co_return out;
    }
    LOG_INFO << "CollectorOrchestrator.activate_config revision=" << newRev
             << " config=" << configId << " devices=" << result.plan["devices"].size();
    nlohmann::json out;
    out["revision"] = newRev;
    out["plan_hash"] = result.plan["plan_hash"];
    co_return out;
}

drogon::Task<nlohmann::json> CollectorOrchestrator::deactivateConfig(int64_t configId) {
    auto exists = co_await db_->execSqlCoro(
        "SELECT id FROM running_equipment_config WHERE id = $1", configId);
    if (exists.size() == 0)
        throw ibmcp::BizError(ibmcp::err::NOT_FOUND, "config not found");
    co_await setStatus(configId, 2);
    int64_t newRev = co_await nextRevision();
    auto plan = co_await compileFull(newRev);
    co_await apply(plan, configId);
    nlohmann::json out;
    out["revision"] = newRev;
    out["device_count"] = static_cast<int>(plan["devices"].size());
    out["message"] = "config deactivated";
    co_return out;
}

drogon::Task<nlohmann::json> CollectorOrchestrator::compileOnly(int64_t configId) {
    int64_t newRev = co_await nextRevision();
    try {
        co_return co_await compiler_.compileForConfig(configId, newRev);
    } catch (const ibmcp::BizError &) {
        throw;
    } catch (const std::exception &e) {
        throw ibmcp::BizError(ibmcp::err::VALIDATION_FAILED, e.what());
    }
}

drogon::Task<nlohmann::json> CollectorOrchestrator::recompileAll() {
    int64_t newRev = co_await nextRevision();
    auto plan = co_await compileFull(newRev);
    co_await apply(plan, 0);
    nlohmann::json out;
    out["revision"] = newRev;
    out["device_count"] = static_cast<int>(plan["devices"].size());
    co_return out;
}

drogon::Task<void> CollectorOrchestrator::apply(const nlohmann::json &plan, int64_t configId) {
    int64_t revision = plan["revision"].get<int64_t>();
    std::string planHash = plan.value("plan_hash", "");
    if (planHash.empty()) planHash = ibmcp::planHash(plan);

    int ret = 0;
    try {
        ret = collector_.applyPlan(plan, static_cast<int>(revision));
    } catch (const ibmcp::CollectorError &e) {
        collector_err::rethrowAsBiz(e);
    } catch (const std::exception &e) {
        throw ibmcp::BizError(ibmcp::err::INTERNAL, e.what());
    }
    if (ret != 0) {
        throw ibmcp::BizError(4000 + ret, "apply_plan rejected by collector");
    }

    co_await syncRuntimeSnapshot(plan);

    int64_t now = std::time(nullptr);
    if (configId > 0) {
        co_await db_->execSqlCoro(
            "INSERT INTO runtime_plan_revision (revision, config_id, plan_hash, status, applied_at) "
            "VALUES ($1, $2, $3, 1, $4)",
            revision, configId, planHash, now);
    } else {
        co_await db_->execSqlCoro(
            "INSERT INTO runtime_plan_revision (revision, config_id, plan_hash, status, applied_at) "
            "VALUES ($1, NULL, $2, 1, $3)",
            revision, planHash, now);
    }
}

drogon::Task<void> CollectorOrchestrator::syncRuntimeSnapshot(const nlohmann::json &plan) {
    int64_t now = std::time(nullptr);
    auto tx = db_->newTransaction();
    try {
        for (const auto &dev : plan["devices"]) {
            std::string deviceCode = dev["device_key"].get<std::string>();
            std::string proto = dev.value("protocol", "");
            std::transform(proto.begin(), proto.end(), proto.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            std::string connDump = dev.value("connection", nlohmann::json::object()).dump();
            co_await tx->execSqlCoro(
                "INSERT INTO using_devices "
                "(device_code, device_name, equipment_code, equipment_name, protocol_code, "
                " device_connection, status, created_at, updated_at) "
                "VALUES ($1, $2, $3, $4, $5, $6::jsonb, 0, $7, $7) "
                "ON CONFLICT (device_code) DO UPDATE SET "
                " device_name = EXCLUDED.device_name, equipment_code = EXCLUDED.equipment_code, "
                " equipment_name = EXCLUDED.equipment_name, protocol_code = EXCLUDED.protocol_code, "
                " device_connection = EXCLUDED.device_connection, updated_at = EXCLUDED.updated_at",
                deviceCode, dev.value("device_name", deviceCode),
                dev.value("equipment_code", ""), dev.value("equipment_name", ""),
                proto, connDump, now);

            std::vector<std::string> addresses;
            for (const auto &pt : dev["points"]) {
                std::string address = pt["address"].get<std::string>();
                addresses.push_back(address);
                int cycleMs = pt.value("cycle_ms", 1000);
                std::string itemName = pt.value("item_name", "");
                if (itemName.empty()) itemName = pt.value("item_code", address);
                co_await tx->execSqlCoro(
                    "INSERT INTO using_points "
                    "(device_code, using_point_id, item_name, address, data_type, read_write, "
                    " unit, scale, \"offset\", collection_cycle, cycle_ms, "
                    " alarm_high, alarm_low, alarm_enabled, enabled, created_at, updated_at) "
                    "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, true, $15, $15) "
                    "ON CONFLICT (device_code, address) DO UPDATE SET "
                    " using_point_id = EXCLUDED.using_point_id, item_name = EXCLUDED.item_name, "
                    " data_type = EXCLUDED.data_type, read_write = EXCLUDED.read_write, "
                    " unit = EXCLUDED.unit, scale = EXCLUDED.scale, "
                    " \"offset\" = EXCLUDED.\"offset\", collection_cycle = EXCLUDED.collection_cycle, "
                    " cycle_ms = EXCLUDED.cycle_ms, alarm_high = EXCLUDED.alarm_high, "
                    " alarm_low = EXCLUDED.alarm_low, alarm_enabled = EXCLUDED.alarm_enabled, "
                    " enabled = true, updated_at = EXCLUDED.updated_at",
                    deviceCode, pt["point_id"].get<int64_t>(), itemName, address,
                    pt.value("data_type", "float"), pt.value("read_write", "R"),
                    pt.value("unit", ""), pt.value("scale", 1.0), pt.value("offset", 0.0),
                    std::max(1, cycleMs / 1000), cycleMs,
                    pt.value("alarm_high", 0.0), pt.value("alarm_low", 0.0),
                    pt.value("alarm_enabled", false), now);
            }
            if (!addresses.empty()) {
                std::string pgArr = "{";
                for (size_t ai = 0; ai < addresses.size(); ++ai) {
                    if (ai) pgArr += ",";
                    std::string escaped;
                    for (char c : addresses[ai]) {
                        if (c == '"') escaped += "\"\"";
                        else escaped += c;
                    }
                    pgArr += "\"" + escaped + "\"";
                }
                pgArr += "}";
                co_await tx->execSqlCoro(
                    "UPDATE using_points SET enabled = false, updated_at = $2 "
                    "WHERE device_code = $1 AND NOT (address = ANY($3::text[]))",
                    deviceCode, now, pgArr);
            } else {
                co_await tx->execSqlCoro(
                    "UPDATE using_points SET enabled = false, updated_at = $2 "
                    "WHERE device_code = $1",
                    deviceCode, now);
            }
        }
    } catch (...) {
        tx->rollback();
        throw;
    }
    LOG_INFO << "runtime snapshot synced revision=" << plan.value("revision", 0)
             << " devices=" << plan["devices"].size();
}

}  // namespace ibmcp