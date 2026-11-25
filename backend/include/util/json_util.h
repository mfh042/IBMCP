#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/orm/Result.h>
#include <json/json.h>
#include <nlohmann/json.hpp>

#include <charconv>
#include <memory>
#include <string>

namespace ibmcp::util {

inline Json::Value fieldToJson(const drogon::orm::Field &f) {
    if (f.isNull()) return Json::Value();
    std::string s = f.as<std::string>();
    if (s.empty()) return "";
    if (s == "true") return true;
    if (s == "false") return false;
    int64_t iv = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), iv);
    if (ec == std::errc() && p == s.data() + s.size()) {
        return static_cast<Json::Int64>(iv);
    }
    double dv = 0;
    try {
        size_t pos = 0;
        dv = std::stod(s, &pos);
        if (pos == s.size()) return dv;
    } catch (...) {
    }
    if (s.size() > 1 && (s.front() == '{' || s.front() == '[')) {
        Json::CharReaderBuilder b;
        std::string errs;
        Json::Value parsed;
        std::unique_ptr<Json::CharReader> reader(b.newCharReader());
        if (reader->parse(s.data(), s.data() + s.size(), &parsed, &errs)) return parsed;
    }
    return s;
}

inline Json::Value rowToJson(const drogon::orm::Row &row) {
    Json::Value obj(Json::objectValue);
    for (size_t i = 0; i < row.size(); ++i) {
        obj[row[i].name()] = fieldToJson(row[i]);
    }
    return obj;
}

inline Json::Value resultToJson(const drogon::orm::Result &r) {
    Json::Value arr(Json::arrayValue);
    for (const auto &row : r) {
        arr.append(rowToJson(row));
    }
    return arr;
}

inline nlohmann::json toNlohmann(const Json::Value &v) {
    switch (v.type()) {
        case Json::nullValue: return nullptr;
        case Json::intValue: return v.asInt64();
        case Json::uintValue: return v.asUInt64();
        case Json::realValue: return v.asDouble();
        case Json::stringValue: return v.asString();
        case Json::booleanValue: return v.asBool();
        case Json::arrayValue: {
            nlohmann::json a = nlohmann::json::array();
            for (const auto &e : v) a.push_back(toNlohmann(e));
            return a;
        }
        case Json::objectValue: {
            nlohmann::json o = nlohmann::json::object();
            for (auto it = v.begin(); it != v.end(); ++it) {
                o[it.key().asString()] = toNlohmann(*it);
            }
            return o;
        }
        default: return nullptr;
    }
}

inline Json::Value fromNlohmann(const nlohmann::json &j) {
    if (j.is_null()) return Json::Value();
    if (j.is_boolean()) return j.get<bool>();
    if (j.is_number_integer()) return static_cast<Json::Int64>(j.get<int64_t>());
    if (j.is_number_unsigned()) return static_cast<Json::UInt64>(j.get<uint64_t>());
    if (j.is_number_float()) return j.get<double>();
    if (j.is_string()) return j.get<std::string>();
    if (j.is_array()) {
        Json::Value a(Json::arrayValue);
        for (const auto &e : j) a.append(fromNlohmann(e));
        return a;
    }
    Json::Value o(Json::objectValue);
    for (auto it = j.begin(); it != j.end(); ++it) {
        o[it.key()] = fromNlohmann(it.value());
    }
    return o;
}

inline nlohmann::json reqBodyToNlohmann(drogon::HttpRequestPtr req) {
    const auto &j = req->getJsonObject();
    if (!j) return nlohmann::json::object();
    return toNlohmann(*j);
}

}  // namespace ibmcp::util