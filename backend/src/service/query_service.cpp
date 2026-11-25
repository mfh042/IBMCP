#include "service/query_service.h"

#include "core/errors.h"
#include "util/json_util.h"

#include <algorithm>
#include <map>

namespace ibmcp::service {

namespace {
const std::map<std::string, std::string> kBucketTrunc = {
    {"1min", "date_trunc('minute', ts)"},
    {"5min", "date_trunc('hour', ts) + floor(extract(minute from ts) / 5) * interval '5 minutes'"},
    {"1h", "date_trunc('hour', ts)"},
    {"1d", "date_trunc('day', ts)"},
};
const std::map<std::string, std::string> kAggFunc = {
    {"avg", "AVG"}, {"min", "MIN"}, {"max", "MAX"}, {"sum", "SUM"}, {"count", "COUNT"},
};
}  // namespace

drogon::Task<Json::Value> QueryService::history(int64_t pointId, int64_t startTs, int64_t endTs,
                                                std::string bucket, std::string func, int64_t limit) {
    if (endTs <= startTs)
        throw ibmcp::BizError(ibmcp::err::BAD_REQUEST, "end_ts must be greater than start_ts");

    Json::Value items;
    if (bucket == "raw") {
        auto rows = co_await db_->execSqlCoro(
            "SELECT EXTRACT(EPOCH FROM ts)::bigint AS ts, point_id, value, quality, raw_value "
            "FROM time_series_data "
            "WHERE point_id = $1 AND ts >= to_timestamp($2::bigint) AND ts <= to_timestamp($3::bigint) "
            "ORDER BY ts ASC LIMIT $4",
            pointId, startTs, endTs, limit);
        items = util::resultToJson(rows);
    } else {
        auto bit = kBucketTrunc.find(bucket);
        if (bit == kBucketTrunc.end())
            throw ibmcp::BizError(ibmcp::err::BAD_REQUEST, "unknown bucket: " + bucket);
        auto fit = kAggFunc.find(func);
        std::string agg = fit == kAggFunc.end() ? "AVG" : fit->second;
        std::string sql =
            "SELECT EXTRACT(EPOCH FROM (" + bit->second + "))::bigint AS ts, " + agg +
            "(value) AS value, COUNT(*) AS sample_count, "
            "(array_agg(quality ORDER BY ts DESC))[1] AS last_quality "
            "FROM time_series_data "
            "WHERE point_id = $1 AND ts >= to_timestamp($2::bigint) AND ts <= to_timestamp($3::bigint) "
            "GROUP BY 1 ORDER BY ts ASC LIMIT $4";
        auto rows = co_await db_->execSqlCoro(sql, pointId, startTs, endTs, limit);
        items = util::resultToJson(rows);
    }

    Json::Value out(Json::objectValue);
    out["point_id"] = pointId;
    out["start_ts"] = static_cast<Json::Int64>(startTs);
    out["end_ts"] = static_cast<Json::Int64>(endTs);
    out["bucket"] = bucket;
    out["func"] = func;
    out["items"] = items;
    out["total"] = static_cast<int>(items.size());
    co_return out;
}

drogon::Task<Json::Value> QueryService::latest(const std::string &deviceCode,
                                               const std::string &pointIds) {
    std::vector<int64_t> pidList;
    if (!pointIds.empty()) {
        size_t start = 0;
        while (start <= pointIds.size()) {
            size_t comma = pointIds.find(',', start);
            std::string tok = pointIds.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            std::string t;
            for (char c : tok) if (!std::isspace(static_cast<unsigned char>(c))) t.push_back(c);
            if (!t.empty()) {
                try { pidList.push_back(std::stoll(t)); } catch (...) {}
            }
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }

    std::string pidArray = "{";
    for (size_t i = 0; i < pidList.size(); ++i) {
        if (i) pidArray += ",";
        pidArray += std::to_string(pidList[i]);
    }
    pidArray += "}";

    static const char *kCols =
        "SELECT DISTINCT ON (t.point_id) p.device_code, t.point_id, t.value, t.quality, "
        "EXTRACT(EPOCH FROM t.ts)::bigint AS ts, p.item_name "
        "FROM time_series_data t JOIN using_points p ON p.id = t.point_id ";

    // drogon::orm::Result is not default-constructible; build it via a coroutine
    // lambda. Drogon 1.9.5 cannot bind std::vector<int64_t> as an array param,
    // so pidList is passed as a PostgreSQL array-literal string cast to bigint[].
    auto query = [&]() -> drogon::Task<drogon::orm::Result> {
        if (!deviceCode.empty()) {
            if (!pidList.empty()) {
                co_return co_await db_->execSqlCoro(
                    std::string(kCols) +
                        "WHERE p.device_code = $1 AND t.point_id = ANY($2::bigint[]) "
                        "ORDER BY t.point_id, t.ts DESC",
                    deviceCode, pidArray);
            }
            co_return co_await db_->execSqlCoro(
                std::string(kCols) + "WHERE p.device_code = $1 ORDER BY t.point_id, t.ts DESC",
                deviceCode);
        }
        if (!pidList.empty()) {
            co_return co_await db_->execSqlCoro(
                std::string(kCols) +
                    "WHERE t.point_id = ANY($1::bigint[]) ORDER BY t.point_id, t.ts DESC",
                pidArray);
        }
        co_return co_await db_->execSqlCoro(
            std::string(kCols) + "ORDER BY t.point_id, t.ts DESC");
    };
    auto rows = co_await query();

    Json::Value out(Json::objectValue);
    out["device_code"] = deviceCode.empty() ? Json::Value() : Json::Value(deviceCode);
    Json::Value arr(Json::arrayValue);
    for (const auto &p : pidList) arr.append(static_cast<Json::Int64>(p));
    out["point_ids"] = pidList.empty() ? Json::Value() : arr;
    out["items"] = util::resultToJson(rows);
    out["total"] = static_cast<int>(rows.size());
    co_return out;
}

}  // namespace ibmcp::service