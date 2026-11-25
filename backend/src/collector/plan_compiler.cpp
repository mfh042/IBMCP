#include "collector/plan_compiler.h"

#include "core/errors.h"

#include <openssl/sha.h>

#include <algorithm>
#include <cstdio>
#include <cctype>

namespace ibmcp {

namespace {

const char *kCompileSql =
    "SELECT c.id AS config_id, c.config_code AS config_code, "
    "c.equipment_code AS equipment_code, c.equipment_name AS equipment_name, "
    "r.route_code AS route_code, r.id AS route_config_id, "
    "d.id AS binding_id, d.step_code AS step_code, "
    "d.device_code AS device_code, d.device_name AS device_name, "
    "d.protocol_code AS protocol_code, d.device_connection AS device_connection, "
    "d.connect_timeout_ms AS connect_timeout_ms, d.read_timeout_ms AS read_timeout_ms, "
    "col.id AS collection_id, col.item_code AS item_code, "
    "COALESCE(col.item_name, it.item_name) AS item_name, "
    "COALESCE(NULLIF(col.address_override, ''), it.address_rule) AS address, "
    "COALESCE(col.collection_cycle, it.collection_cycle, 1) * 1000 AS cycle_ms, "
    "COALESCE(col.data_type, it.data_type, 'float') AS data_type, "
    "COALESCE(col.read_write, it.read_write, 'R') AS read_write, "
    "COALESCE(col.scale, it.scale, 1.0) AS scale, "
    "COALESCE(col.\"offset\", it.\"offset\", 0.0) AS \"offset\", "
    "COALESCE(col.alarm_high, it.alarm_high, 0.0) AS alarm_high, "
    "COALESCE(col.alarm_low, it.alarm_low, 0.0) AS alarm_low, "
    "COALESCE(col.alarm_enabled, it.alarm_enabled, false) AS alarm_enabled, "
    "COALESCE(it.unit, '') AS unit "
    "FROM running_equipment_config c "
    "JOIN running_equipment_route r ON r.config_id = c.id AND r.enabled = 1 "
    "JOIN running_route_device d ON d.route_config_id = r.id AND d.enabled = 1 "
    "JOIN running_device_collection col ON col.device_binding_id = d.id AND col.enabled = 1 "
    "JOIN base_device_data_item_dict it ON it.item_code = col.item_code "
    "{where} "
    "ORDER BY d.id, col.id";

std::string getStr(const drogon::orm::Row &row, const char *col, const std::string &def = "") {
    if (row[col].isNull()) return def;
    return row[col].as<std::string>();
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

nlohmann::json parseJsonOr(const std::string &s, const nlohmann::json &def) {
    std::string t = trim(s);
    if (t.empty()) return def;
    try {
        return nlohmann::json::parse(t);
    } catch (...) {
        return def;
    }
}

std::string canonical(const nlohmann::json &j) {
    if (j.is_object()) {
        std::vector<std::string> keys;
        for (auto it = j.begin(); it != j.end(); ++it) keys.push_back(it.key());
        std::sort(keys.begin(), keys.end());
        std::string out = "{";
        for (size_t i = 0; i < keys.size(); ++i) {
            if (i) out += ",";
            out += "\"" + keys[i] + "\":" + canonical(j[keys[i]]);
        }
        return out + "}";
    }
    if (j.is_array()) {
        std::string out = "[";
        for (size_t i = 0; i < j.size(); ++i) {
            if (i) out += ",";
            out += canonical(j[i]);
        }
        return out + "]";
    }
    return j.dump();
}

}  // namespace

std::string planHash(const nlohmann::json &plan) {
    nlohmann::json payload = nlohmann::json::object();
    for (auto it = plan.begin(); it != plan.end(); ++it) {
        if (it.key() != "revision") payload[it.key()] = it.value();
    }
    std::string s = canonical(payload);
    unsigned char md[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(s.data()), s.size(), md);
    char buf[33];
    for (int i = 0; i < 16; ++i) std::sprintf(buf + i * 2, "%02x", md[i]);
    buf[32] = 0;
    return std::string(buf);
}

drogon::Task<nlohmann::json> PlanCompiler::compileForConfig(int64_t configId, int64_t revision) {
    std::string sql = kCompileSql;
    sql.replace(sql.find("{where}"), 7, "WHERE c.id = $1 AND c.config_status = 1");
    auto rows = co_await db_->execSqlCoro(sql, configId);
    if (rows.size() == 0) {
        auto check = co_await db_->execSqlCoro(
            "SELECT config_status FROM running_equipment_config WHERE id = $1", configId);
        if (check.size() == 0)
            throw ibmcp::BizError(ibmcp::err::NOT_FOUND, "config not found");
        if (check[0]["config_status"].as<int>() != 1)
            throw ibmcp::BizError(ibmcp::err::VALIDATION_FAILED,
                                  "config is not active (config_status != 1)");
    }
    co_return buildPlan(rows, revision);
}

drogon::Task<nlohmann::json> PlanCompiler::compileFull(int64_t revision) {
    std::string sql = kCompileSql;
    sql.replace(sql.find("{where}"), 7, "WHERE c.config_status = 1");
    auto rows = co_await db_->execSqlCoro(sql);
    co_return buildPlan(rows, revision);
}

nlohmann::json PlanCompiler::buildPlan(const drogon::orm::Result &rows, int64_t revision) {
    nlohmann::json devices = nlohmann::json::object();
    for (const auto &row : rows) {
        std::string deviceKey = getStr(row, "device_code");
        std::string address = trim(getStr(row, "address"));
        if (address.empty()) {
            throw ibmcp::BizError(ibmcp::err::VALIDATION_FAILED,
                                  "point " + getStr(row, "item_code") + " on device " +
                                      deviceKey + " has empty address");
        }
        double scale = row["scale"].isNull() ? 1.0 : row["scale"].as<double>();
        if (scale == 0.0) {
            throw ibmcp::BizError(ibmcp::err::VALIDATION_FAILED,
                                  "scale must not be 0 for item " + getStr(row, "item_code") +
                                      " on device " + deviceKey);
        }
        if (!devices.contains(deviceKey)) {
            nlohmann::json dev;
            dev["device_key"] = deviceKey;
            dev["device_name"] = getStr(row, "device_name", deviceKey);
            dev["equipment_code"] = getStr(row, "equipment_code");
            dev["equipment_name"] = getStr(row, "equipment_name");
            dev["route_code"] = getStr(row, "route_code");
            dev["step_code"] = getStr(row, "step_code");
            dev["protocol"] = toLower(getStr(row, "protocol_code"));
            dev["connection"] = parseJsonOr(getStr(row, "device_connection"), nlohmann::json::object());
            dev["connect_timeout_ms"] = row["connect_timeout_ms"].isNull() ? 3000 : row["connect_timeout_ms"].as<int>();
            dev["read_timeout_ms"] = row["read_timeout_ms"].isNull() ? 1000 : row["read_timeout_ms"].as<int>();
            dev["points"] = nlohmann::json::array();
            devices[deviceKey] = dev;
        }
        nlohmann::json point;
        int collectionId = row["collection_id"].as<int>();
        point["point_id"] = collectionId;
        point["point_key"] = deviceKey + ":" + std::to_string(collectionId);
        point["item_code"] = getStr(row, "item_code");
        point["item_name"] = getStr(row, "item_name");
        point["address"] = address;
        point["data_type"] = getStr(row, "data_type", "float");
        point["read_write"] = getStr(row, "read_write", "R");
        point["cycle_ms"] = row["cycle_ms"].isNull() ? 1000 : row["cycle_ms"].as<int>();
        point["scale"] = scale;
        point["offset"] = row["offset"].isNull() ? 0.0 : row["offset"].as<double>();
        point["alarm_high"] = row["alarm_high"].isNull() ? 0.0 : row["alarm_high"].as<double>();
        point["alarm_low"] = row["alarm_low"].isNull() ? 0.0 : row["alarm_low"].as<double>();
        point["alarm_enabled"] = !row["alarm_enabled"].isNull() && row["alarm_enabled"].as<bool>();
        point["unit"] = getStr(row, "unit");

        nlohmann::json &pts = devices[deviceKey]["points"];
        nlohmann::json *existing = nullptr;
        for (auto &p : pts) {
            if (p["address"] == point["address"] && p["data_type"] == point["data_type"]) {
                existing = &p;
                break;
            }
        }
        if (!existing) {
            pts.push_back(point);
        } else {
            int a = (*existing)["cycle_ms"].get<int>();
            int b = point["cycle_ms"].get<int>();
            (*existing)["cycle_ms"] = std::min(a, b);
        }
    }
    nlohmann::json plan;
    plan["revision"] = revision;
    plan["flush_interval_ms"] = 200;
    nlohmann::json devArr = nlohmann::json::array();
    for (auto it = devices.begin(); it != devices.end(); ++it) devArr.push_back(it.value());
    plan["devices"] = devArr;
    plan["plan_hash"] = planHash(plan);
    return plan;
}

}  // namespace ibmcp