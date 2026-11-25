#pragma once

#include <drogon/HttpRequest.h>
#include <json/json.h>
#include <json/writer.h>

#include <cctype>
#include <chrono>
#include <initializer_list>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "collector/collector_bridge.h"
#include "core/auth.h"
#include "core/errors.h"

namespace ibmcp::ctrl {

inline std::string join(const std::vector<std::string> &parts, const std::string &sep) {
    std::string out;
    bool first = true;
    for (const auto &p : parts) {
        if (!first) out += sep;
        first = false;
        out += p;
    }
    return out;
}

inline int getIntParam(drogon::HttpRequestPtr req, const std::string &key, int def) {
    auto v = req->getParameter(key);
    if (v.empty()) return def;
    try {
        return std::stoi(v);
    } catch (...) {
        return def;
    }
}

inline int64_t getInt64Param(drogon::HttpRequestPtr req, const std::string &key, int64_t def) {
    auto v = req->getParameter(key);
    if (v.empty()) return def;
    try {
        return std::stoll(v);
    } catch (...) {
        return def;
    }
}

inline std::string getStrParam(drogon::HttpRequestPtr req, const std::string &key,
                               const std::string &def = "") {
    auto v = req->getParameter(key);
    return v.empty() ? def : v;
}

inline void requireAnyRole(drogon::HttpRequestPtr req,
                           std::initializer_list<std::string> roles) {
    auth::requireAuth(req);
    for (const auto &r : roles) {
        if (auth::hasRole(req, r)) return;
    }
    throw ibmcp::BizError(ibmcp::err::FORBIDDEN, "forbidden: insufficient role");
}

inline std::string operatorOf(drogon::HttpRequestPtr req) {
    auto u = auth::currentUser(req);
    if (!u) return "";
    return u->username.empty() ? u->sub : u->username;
}

inline Json::Value healthToJson(const HealthInfo &h) {
    Json::Value r(Json::objectValue);
    r["threads"] = h.threads;
    r["revision"] = h.revision;
    r["device_count"] = h.device_count;
    r["devices_real"] = h.devices_real;
    r["devices_degraded"] = h.devices_degraded;
    r["point_count"] = h.point_count;
    r["queue_depth"] = h.queue_depth;
    r["dropped"] = h.dropped;
    r["uptime_sec"] = h.uptime_sec;
    Json::Value protos(Json::arrayValue);
    for (const auto &p : h.protocols) protos.append(p);
    r["protocols"] = protos;
    return r;
}

inline int64_t nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

inline std::string jsonValueToParam(const Json::Value &v) {
    if (v.isNull()) return "__IBMCP_NULL__";
    if (v.isBool()) return v.asBool() ? "true" : "false";
    if (v.isInt() || v.isInt64()) return std::to_string(v.asInt64());
    if (v.isUInt() || v.isUInt64()) return std::to_string(v.asUInt64());
    if (v.isDouble()) return std::to_string(v.asDouble());
    if (v.isString()) return v.asString();
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    return Json::writeString(b, v);
}

inline std::string quoteSql(const std::string &v) {
    if (v.empty()) return "''";
    bool allNum = true;
    bool hasDot = false;
    for (size_t i = 0; i < v.size(); ++i) {
        char c = v[i];
        if (i == 0 && c == '-') continue;
        if (std::isdigit(static_cast<unsigned char>(c))) continue;
        if (c == '.' && !hasDot) { hasDot = true; continue; }
        allNum = false; break;
    }
    if (allNum || v == "true" || v == "false" || v == "NULL") return v;
    std::string out = "'";
    for (char c : v) {
        if (c == '\'') out += "''";
        else out += c;
    }
    out += "'";
    return out;
}

using KvList = std::vector<std::pair<std::string, std::string>>;

inline std::string quoteIdent(const std::string &name) {
    std::string out = "\"";
    for (char c : name) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

// Convert request body fields into SQL column/value pairs, keeping only the
// columns listed in `allowed`. Unknown keys are silently dropped so that
// client-supplied field names can never reach SQL as identifiers.
inline KvList bodyToPairs(const Json::Value &body,
                          std::initializer_list<const char *> allowed) {
    KvList out;
    std::set<std::string> allow(allowed.begin(), allowed.end());
    for (auto it = body.begin(); it != body.end(); ++it) {
        std::string key = it.key().asString();
        if (allow.count(key)) out.emplace_back(key, jsonValueToParam(*it));
    }
    return out;
}

inline std::string buildInsert(const std::string &table, KvList data) {
    std::vector<std::string> cols, vals;
    for (auto &kv : data) {
        cols.push_back(quoteIdent(kv.first));
        if (kv.second == "__IBMCP_NULL__") vals.push_back("NULL");
        else vals.push_back(quoteSql(kv.second));
    }
    return "INSERT INTO " + table + " (" + join(cols, ", ") + ") VALUES (" + join(vals, ", ") +
           ") RETURNING *";
}

inline std::string buildUpdate(const std::string &table, KvList data, int64_t id) {
    std::vector<std::string> sets;
    for (auto &kv : data) {
        if (kv.second == "__IBMCP_NULL__") sets.push_back(quoteIdent(kv.first) + " = NULL");
        else sets.push_back(quoteIdent(kv.first) + " = " + quoteSql(kv.second));
    }
    return "UPDATE " + table + " SET " + join(sets, ", ") + " WHERE " + quoteIdent("id") + " = " +
           quoteSql(std::to_string(id)) + " RETURNING *";
}

}  // namespace ibmcp::ctrl