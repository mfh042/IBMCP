#include "service/static_dict_service.h"

#include <algorithm>
#include <chrono>
#include <set>

#include <drogon/drogon.h>

#include <json/writer.h>

#include "controller/common.h"
#include "core/errors.h"
#include "db/db.h"
#include "util/json_util.h"

namespace ibmcp::service {

namespace {

int64_t nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string jsonToString(const Json::Value &v) {
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    return Json::writeString(b, v);
}

bool isValidJsonString(const std::string &s) {
    Json::CharReaderBuilder b;
    Json::Value parsed;
    std::string errs;
    std::unique_ptr<Json::CharReader> r(b.newCharReader());
    return r->parse(s.data(), s.data() + s.size(), &parsed, &errs);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

}  // namespace

StaticDictService &StaticDictService::instance() {
    static StaticDictService inst;
    return inst;
}

const std::map<std::string, ResourceSpec> &StaticDictService::specs() {
    static const std::map<std::string, ResourceSpec> m = {
        {"devices", {"base_device_base_info", "device_code", "device_name"}},
        {"device_categories", {"base_device_category_dict", "category_code", "category_name"}},
        {"brands", {"base_device_brand_model_dict", "brand_code", "brand_name"}},
        {"ports", {"base_device_port_dict", "port_code", "port_name"}},
        {"protocols", {"base_device_protocol_dict", "protocol_code", "protocol_name"}},
        {"data_items", {"base_device_data_item_dict", "item_code", "item_name"}},
        {"equipments", {"base_equipment_base_info", "equipment_code", "equipment_name"}},
        {"equipment_functions", {"base_equipment_function_dict", "function_code", "function_name"}},
        {"process_routes", {"base_process_route_resource", "route_code", "route_name"}},
        {"process_steps", {"base_process_route_step", "step_code", "step_name"}},
    };
    return m;
}

const std::map<std::string, std::vector<std::pair<std::string, std::string>>>
    &StaticDictService::referenceSpecs() {
    static const std::map<std::string, std::vector<std::pair<std::string, std::string>>> m = {
        {"devices", {{"running_route_device", "device_code"},
                      {"base_process_route_step", "device_code"}}},
        {"device_categories",
         {{"base_device_base_info", "category_code"},
          {"base_device_brand_model_dict", "category_code"},
          {"base_device_data_item_dict", "category_code"},
          {"base_equipment_base_info", "category_code"},
          {"base_equipment_function_dict", "category_code"}}},
        {"brands",
         {{"base_device_base_info", "brand_code"},
          {"base_equipment_base_info", "brand_code"}}},
        {"ports", {{"base_device_base_info", "port_code"}}},
        {"protocols", {{"base_device_data_item_dict", "protocol_code"}}},
        {"data_items", {{"running_device_collection", "item_code"}}},
        {"equipments", {{"running_equipment_config", "equipment_code"}}},
        {"equipment_functions", {{"running_equipment_function", "function_code"}}},
        {"process_routes",
         {{"running_equipment_route", "route_code"},
          {"base_process_route_step", "route_code"}}},
        {"process_steps", {}},
    };
    return m;
}

const ResourceSpec &StaticDictService::spec(const std::string &resource) const {
    const auto &s = specs();
    auto it = s.find(resource);
    if (it == s.end()) throw BizError(err::BAD_REQUEST, "unknown static resource: " + resource);
    return it->second;
}

drogon::Task<std::map<std::string, StaticDictService::ColMeta>> StaticDictService::columns(
    const std::string &table) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = cache_.find(table);
        if (it != cache_.end()) co_return it->second;
    }
    auto rows = co_await db::ro()->execSqlCoro(
        "SELECT column_name, data_type, is_nullable, character_maximum_length, column_default "
        "FROM information_schema.columns "
        "WHERE table_schema = 'public' AND table_name = $1",
        table);
    if (rows.empty()) throw BizError(err::INTERNAL, "table metadata missing: " + table);
    std::map<std::string, ColMeta> meta;
    for (const auto &row : rows) {
        ColMeta c;
        c.dataType = row["data_type"].as<std::string>();
        c.nullable = row["is_nullable"].as<std::string>() == "YES";
        if (!row["character_maximum_length"].isNull()) {
            c.hasMaxLen = true;
            c.maxLen = row["character_maximum_length"].as<long>();
        }
        c.hasDefault = !row["column_default"].isNull();
        meta[row["column_name"].as<std::string>()] = c;
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        cache_[table] = meta;
    }
    co_return meta;
}

std::string StaticDictService::coerce(const std::string &resource, const Json::Value &value,
                                      const std::string &col, const ColMeta &meta) const {
    (void)resource;
    const std::string &dt = meta.dataType;
    if (value.isNull()) {
        // bind null via empty string won't work; caller must handle null. Use literal.
        return "__IBMCP_NULL__";
    }
    if (dt == "integer" || dt == "bigint" || dt == "smallint") {
        try {
            return std::to_string(value.asInt64());
        } catch (...) {
            throw BizError(err::VALIDATION_FAILED, "field " + col + " must be an integer");
        }
    }
    if (dt == "double precision" || dt == "real" || dt == "numeric") {
        try {
            return std::to_string(value.asDouble());
        } catch (...) {
            throw BizError(err::VALIDATION_FAILED, "field " + col + " must be a number");
        }
    }
    if (dt == "boolean") {
        if (value.isBool()) return value.asBool() ? "true" : "false";
        if (value.isInt()) {
            int v = value.asInt();
            if (v == 0 || v == 1) return v == 1 ? "true" : "false";
        }
        if (value.isString()) {
            std::string v = lower(value.asString());
            if (v == "1" || v == "true") return "true";
            if (v == "0" || v == "false") return "false";
        }
        throw BizError(err::VALIDATION_FAILED, "field " + col + " must be a boolean");
    }
    if (dt == "json" || dt == "jsonb") {
        if (value.isObject() || value.isArray()) return jsonToString(value);
        if (value.isString()) {
            std::string s = value.asString();
            if (!isValidJsonString(s))
                throw BizError(err::VALIDATION_FAILED, "field " + col + " must be valid JSON");
            return s;
        }
        throw BizError(err::VALIDATION_FAILED,
                       "field " + col + " must be a JSON object/array");
    }
    std::string text = value.asString();
    if (meta.hasMaxLen && static_cast<long>(text.size()) > meta.maxLen) {
        Json::Value d(Json::objectValue);
        d["field"] = col;
        d["length"] = static_cast<Json::Int64>(text.size());
        d["max_length"] = static_cast<Json::Int64>(meta.maxLen);
        throw BizError(err::VALIDATION_FAILED,
                       "field " + col + " exceeds max length " + std::to_string(meta.maxLen), d);
    }
    return text;
}

std::map<std::string, std::string> StaticDictService::validateBody(
    const std::string &resource, const Json::Value &body,
    const std::map<std::string, ColMeta> &meta, bool partial) const {
    static const std::set<std::string> kReadonly = {"id", "created_at", "updated_at"};
    if (!body.isObject())
        throw BizError(err::VALIDATION_FAILED, "request body must be an object");

    std::vector<std::string> unknown;
    for (auto it = body.begin(); it != body.end(); ++it) {
        if (meta.find(it.key().asString()) == meta.end()) unknown.push_back(it.key().asString());
    }
    if (!unknown.empty()) {
        Json::Value d(Json::objectValue);
        Json::Value arr(Json::arrayValue);
        for (const auto &f : unknown) arr.append(f);
        d["fields"] = arr;
        throw BizError(err::VALIDATION_FAILED, "unknown field(s) for " + resource, d);
    }

    std::vector<std::string> readonly;
    std::map<std::string, std::string> data;
    for (auto it = body.begin(); it != body.end(); ++it) {
        std::string k = it.key().asString();
        if (kReadonly.count(k)) {
            readonly.push_back(k);
            continue;
        }
        data[k] = coerce(resource, *it, k, meta.at(k));
    }
    if (!readonly.empty()) {
        Json::Value d(Json::objectValue);
        Json::Value arr(Json::arrayValue);
        for (const auto &f : readonly) arr.append(f);
        d["fields"] = arr;
        throw BizError(err::VALIDATION_FAILED, "read-only field(s) not allowed", d);
    }
    if (data.empty()) throw BizError(err::VALIDATION_FAILED, "empty request body");

    if (!partial) {
        std::vector<std::string> missing;
        for (const auto &[col, m] : meta) {
            if (kReadonly.count(col)) continue;
            if (m.nullable || m.hasDefault) continue;
            if (data.find(col) == data.end()) missing.push_back(col);
        }
        if (!missing.empty()) {
            Json::Value d(Json::objectValue);
            Json::Value arr(Json::arrayValue);
            for (const auto &f : missing) arr.append(f);
            d["fields"] = arr;
            throw BizError(err::VALIDATION_FAILED, "missing required field(s)", d);
        }
    }
    return data;
}

drogon::Task<void> StaticDictService::crossCheck(
    const std::string &resource, const std::map<std::string, std::string> &data) const {
    auto get = [&](const std::string &k) -> std::string {
        auto it = data.find(k);
        return it == data.end() ? std::string() : it->second;
    };
    auto exists = [&](const std::string &table, const std::string &col, const std::string &value,
                      const std::string &label) -> drogon::Task<void> {
        auto rows = co_await db::ro()->execSqlCoro(
            "SELECT 1 FROM " + table + " WHERE " + col + " = $1 LIMIT 1", value);
        if (rows.empty()) throw BizError(err::BAD_REQUEST, label + " not found: " + value);
    };

    static const std::set<std::string> kCatResources = {
        "devices", "equipments", "equipment_functions", "data_items", "brands"};
    std::string categoryCode = get("category_code");
    if (!categoryCode.empty() && kCatResources.count(resource)) {
        co_await exists("base_device_category_dict", "category_code", categoryCode,
                        "category_code");
    }
    std::string protocolCode = get("protocol_code");
    if (!protocolCode.empty() && (resource == "data_items" || resource == "devices")) {
        co_await exists("base_device_protocol_dict", "protocol_code", protocolCode,
                        "protocol_code");
    }
    std::string portCode = get("port_code");
    if (!portCode.empty() && resource == "devices") {
        co_await exists("base_device_port_dict", "port_code", portCode, "port_code");
    }
    std::string brandCode = get("brand_code");
    std::string modelCode = get("model_code");
    if (!brandCode.empty() && !modelCode.empty()) {
        auto rows = co_await db::ro()->execSqlCoro(
            "SELECT 1 FROM base_device_brand_model_dict WHERE brand_code = $1 AND model_code = $2",
            brandCode, modelCode);
        if (rows.empty())
            throw BizError(err::BAD_REQUEST,
                           "brand model not found: " + brandCode + "/" + modelCode);
    }
    std::string routeCode = get("route_code");
    if (!routeCode.empty() && resource == "process_steps") {
        co_await exists("base_process_route_resource", "route_code", routeCode, "route_code");
    }
    auto ftIt = data.find("function_type");
    if (ftIt != data.end()) {
        static const std::set<std::string> kTypes = {"采集", "控制", "报警", "追溯"};
        if (!kTypes.count(ftIt->second))
            throw BizError(err::VALIDATION_FAILED,
                           "function_type must be one of 报警/控制/采集/追溯");
    }
    if (resource == "data_items") {
        auto rw = get("read_write");
        if (data.count("read_write") && rw != "R" && rw != "W" && rw != "RW")
            throw BizError(err::VALIDATION_FAILED, "read_write must be one of R/W/RW");
        if (data.count("scale")) {
            try {
                if (std::stod(get("scale")) == 0.0)
                    throw BizError(err::VALIDATION_FAILED, "scale must not be 0");
            } catch (const BizError &) {
                throw;
            } catch (...) {
                throw BizError(err::VALIDATION_FAILED, "scale must be a number");
            }
        }
        if (data.count("collection_cycle")) {
            try {
                if (std::stoll(get("collection_cycle")) < 1)
                    throw BizError(err::VALIDATION_FAILED,
                                   "collection_cycle must be >= 1 second");
            } catch (const BizError &) {
                throw;
            } catch (...) {
                throw BizError(err::VALIDATION_FAILED, "collection_cycle must be an integer");
            }
        }
    }
    if (data.count("status")) {
        try {
            int v = std::stoi(get("status"));
            if (v != 0 && v != 1)
                throw BizError(err::VALIDATION_FAILED, "status must be 0 or 1");
        } catch (const BizError &) {
            throw;
        } catch (...) {
            throw BizError(err::VALIDATION_FAILED, "status must be 0 or 1");
        }
    }
    co_return;
}

drogon::Task<void> StaticDictService::checkReferences(const std::string &resource,
                                                      int64_t rowId) const {
    const auto &sp = spec(resource);
    auto row = co_await db::ro()->execSqlCoro(
        "SELECT * FROM " + sp.table + " WHERE id = $1", rowId);
    if (row.empty()) throw BizError(err::NOT_FOUND, resource + " row not found");
    auto r = row[0];

    Json::Value refs(Json::arrayValue);
    auto appendRef = [&](const std::string &table, int64_t cnt) {
        if (cnt > 0) {
            Json::Value o(Json::objectValue);
            o["table"] = table;
            o["count"] = static_cast<Json::Int64>(cnt);
            refs.append(o);
        }
    };

    if (resource == "process_steps") {
        auto cnt = co_await db::ro()->execSqlCoro(
            "SELECT COUNT(*)::bigint AS cnt FROM running_route_device "
            "WHERE route_code = $1 AND step_code = $2",
            r["route_code"].as<std::string>(), r["step_code"].as<std::string>());
        appendRef("running_route_device", cnt[0]["cnt"].as<int64_t>());
    } else if (resource == "brands") {
        auto cnt = co_await db::ro()->execSqlCoro(
            "SELECT COUNT(*)::bigint AS cnt FROM base_device_base_info "
            "WHERE brand_code = $1 AND model_code = $2",
            r["brand_code"].as<std::string>(), r["model_code"].as<std::string>());
        appendRef("base_device_base_info", cnt[0]["cnt"].as<int64_t>());
    } else {
        const auto &specs = referenceSpecs();
        auto it = specs.find(resource);
        if (it != specs.end()) {
            for (const auto &[refTable, refCol] : it->second) {
                auto cnt = co_await db::ro()->execSqlCoro(
                    "SELECT COUNT(*)::bigint AS cnt FROM " + refTable + " WHERE " + refCol +
                        " = $1",
                    r[sp.code].as<std::string>());
                appendRef(refTable, cnt[0]["cnt"].as<int64_t>());
            }
        }
    }
    if (!refs.empty()) {
        Json::Value d(Json::objectValue);
        d["references"] = refs;
        throw BizError(err::CONFLICT, resource + " is referenced and cannot be deleted", d);
    }
    co_return;
}

drogon::Task<Json::Value> StaticDictService::bootstrap() {
    Json::Value out(Json::objectValue);
    for (const auto &[resource, sp] : specs()) {
        auto meta = co_await columns(sp.table);
        (void)meta;
        auto rows = co_await db::ro()->execSqlCoro(
            "SELECT * FROM " + sp.table + " ORDER BY id");
        out[resource] = util::resultToJson(rows);
    }
    co_return out;
}

drogon::Task<Json::Value> StaticDictService::page(
    const std::string &resource, int page, int size, const std::string &keyword,
    const std::map<std::string, std::string> &filters) {
    const auto &sp = spec(resource);
    auto meta = co_await columns(sp.table);

    std::vector<std::string> unknown;
    for (const auto &[k, v] : filters) {
        (void)v;
        if (meta.find(k) == meta.end()) unknown.push_back(k);
    }
    if (!unknown.empty()) {
        Json::Value d(Json::objectValue);
        Json::Value arr(Json::arrayValue);
        for (const auto &f : unknown) arr.append(f);
        d["fields"] = arr;
        throw BizError(err::VALIDATION_FAILED, "unknown filter field(s)", d);
    }

    std::vector<std::string> clauses;
    for (const auto &[col, value] : filters) {
        clauses.push_back(col + " = " + ctrl::quoteSql(value));
    }
    if (!keyword.empty()) {
        std::string kw = ctrl::quoteSql("%" + keyword + "%");
        clauses.push_back("(" + sp.code + " ILIKE " + kw + " OR " + sp.name + " ILIKE " + kw +
                          ")");
    }
    std::string where = clauses.empty() ? "" : "WHERE " + ctrl::join(clauses, " AND ");

    std::string sql = "SELECT * FROM " + sp.table + " " + where + " ORDER BY id LIMIT " +
                      std::to_string(size) + " OFFSET " + std::to_string((page - 1) * size);
    auto rows = co_await db::ro()->execSqlCoro(sql);

    int64_t total = 0;
    {
        auto cnt = co_await db::ro()->execSqlCoro(
            "SELECT COUNT(*)::bigint AS c FROM " + sp.table + " " + where);
        if (!cnt.empty()) total = cnt[0]["c"].as<int64_t>();
    }
    Json::Value d(Json::objectValue);
    d["items"] = util::resultToJson(rows);
    d["total"] = static_cast<Json::Int64>(total);
    co_return d;
}

drogon::Task<Json::Value> StaticDictService::all(const std::string &resource) {
    const auto &sp = spec(resource);
    auto rows = co_await db::ro()->execSqlCoro("SELECT * FROM " + sp.table + " ORDER BY id");
    Json::Value d(Json::objectValue);
    d["items"] = util::resultToJson(rows);
    co_return d;
}

drogon::Task<Json::Value> StaticDictService::byCode(const std::string &resource,
                                                    const std::string &code) {
    const auto &sp = spec(resource);
    auto rows = co_await db::ro()->execSqlCoro(
        "SELECT * FROM " + sp.table + " WHERE " + sp.code + " = $1 LIMIT 1", code);
    if (rows.empty()) throw BizError(err::NOT_FOUND, resource + " not found: " + code);
    co_return util::resultToJson(rows)[0];
}

drogon::Task<Json::Value> StaticDictService::byId(const std::string &resource, int64_t rowId) {
    const auto &sp = spec(resource);
    auto rows = co_await db::ro()->execSqlCoro(
        "SELECT * FROM " + sp.table + " WHERE id = $1", rowId);
    if (rows.empty()) throw BizError(err::NOT_FOUND, resource + " row not found");
    co_return util::resultToJson(rows)[0];
}

drogon::Task<Json::Value> StaticDictService::create(const std::string &resource,
                                                    const Json::Value &body) {
    const auto &sp = spec(resource);
    auto meta = co_await columns(sp.table);
    auto data = validateBody(resource, body, meta, false);
    co_await crossCheck(resource, data);

    int64_t now = nowEpoch();
    data["created_at"] = std::to_string(now);
    data["updated_at"] = std::to_string(now);

    std::vector<std::string> cols;
    std::vector<std::string> vals;
    for (const auto &[k, v] : data) {
        cols.push_back(k);
        vals.push_back(v == "__IBMCP_NULL__" ? "NULL" : ctrl::quoteSql(v));
    }
    std::string sql = "INSERT INTO " + sp.table + " (" + ctrl::join(cols, ", ") + ") VALUES (" +
                      ctrl::join(vals, ", ") + ") RETURNING *";
    try {
        auto rows = co_await db::rw()->execSqlCoro(sql);
        if (rows.empty()) throw BizError(err::INTERNAL, "insert failed");
        co_return util::resultToJson(rows)[0];
    } catch (const BizError &) {
        throw;
    } catch (const std::exception &e) {
        std::string msg = lower(e.what());
        if (msg.find("unique") != std::string::npos || msg.find("duplicate") != std::string::npos)
            throw BizError(err::CONFLICT, sp.code + " already exists");
        if (msg.find("foreign key") != std::string::npos)
            throw BizError(err::BAD_REQUEST, "referenced row not found");
        throw BizError(err::INTERNAL, "insert failed");
    }
}

drogon::Task<Json::Value> StaticDictService::update(const std::string &resource, int64_t rowId,
                                                    const Json::Value &body) {
    const auto &sp = spec(resource);
    auto meta = co_await columns(sp.table);
    auto data = validateBody(resource, body, meta, true);
    co_await crossCheck(resource, data);

    data["updated_at"] = std::to_string(nowEpoch());
    std::vector<std::string> sets;
    for (const auto &[k, v] : data) {
        if (v == "__IBMCP_NULL__") {
            sets.push_back(k + " = NULL");
        } else {
            sets.push_back(k + " = " + ctrl::quoteSql(v));
        }
    }
    std::string sql = "UPDATE " + sp.table + " SET " + ctrl::join(sets, ", ") + " WHERE id = " +
                      ctrl::quoteSql(std::to_string(rowId)) + " RETURNING *";
    try {
        auto rows = co_await db::rw()->execSqlCoro(sql);
        if (rows.empty()) throw BizError(err::NOT_FOUND, resource + " row not found");
        co_return util::resultToJson(rows)[0];
    } catch (const BizError &) {
        throw;
    } catch (const std::exception &e) {
        std::string msg = lower(e.what());
        if (msg.find("unique") != std::string::npos || msg.find("duplicate") != std::string::npos)
            throw BizError(err::CONFLICT, sp.code + " already exists");
        throw BizError(err::INTERNAL, "update failed");
    }
}

drogon::Task<Json::Value> StaticDictService::remove(const std::string &resource, int64_t rowId) {
    const auto &sp = spec(resource);
    co_await checkReferences(resource, rowId);
    bool ok = false;
    try {
        auto rows = co_await db::rw()->execSqlCoro(
            "DELETE FROM " + sp.table + " WHERE id = $1 RETURNING id", rowId);
        ok = !rows.empty();
    } catch (const std::exception &e) {
        std::string msg = lower(e.what());
        if (msg.find("foreign") != std::string::npos ||
            msg.find("referenced") != std::string::npos)
            throw BizError(err::CONFLICT, "referenced by running data, cannot delete");
        throw BizError(err::INTERNAL, "delete failed");
    }
    Json::Value d(Json::objectValue);
    d["ok"] = ok;
    co_return d;
}

}  // namespace ibmcp::service