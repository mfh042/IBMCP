#include "collector/sample_pump.h"

#include "util/json_util.h"

#include <drogon/drogon.h>
#include <libpq-fe.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace ibmcp {

namespace {

std::string pgTs(int64_t tsMs) {
    time_t t = static_cast<time_t>(tsMs / 1000);
    struct tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return std::string(buf) + "+00";
}

std::string escapeCopy(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\\': out += "\\\\"; break;
            default: out += c;
        }
    }
    return out;
}

std::string fmtDouble(double v) {
    std::ostringstream os;
    os << std::setprecision(15) << v;
    return os.str();
}

struct AlarmState {
    int level = 0;
    int state = 0;
    int64_t triggered_at = 0;
    double value = 0.0;
};

class ActiveAlarmTracker {
  public:
    nlohmann::json checkAndDecide(const std::string &deviceCode, const std::string &pointAddress,
                                  const std::string &pointName, double value,
                                  const std::string &quality, double alarmHigh,
                                  double alarmLow, bool alarmEnabled, int64_t now) {
        if (!alarmEnabled || quality != "Good") return nullptr;
        std::string key = deviceCode + "\x1f" + pointAddress;
        auto it = active_.find(key);
        bool has = (it != active_.end());
        AlarmState curr = has ? it->second : AlarmState{};

        nlohmann::json triggered = nullptr;
        if (value > alarmHigh || value < alarmLow) {
            int level = 2;
            double span = (alarmHigh > alarmLow) ? (alarmHigh - alarmLow) : 0.0;
            if (span > 0) {
                double boundary = (value > alarmHigh) ? alarmHigh : alarmLow;
                if (std::abs(value - boundary) > span * 0.5) level = 3;
            }
            if (!has || curr.state != 1) {
                curr.level = level;
                curr.state = 1;
                curr.triggered_at = now;
                curr.value = value;
                triggered = {
                    {"device_code", deviceCode},
                    {"point_address", pointAddress},
                    {"point_name", pointName},
                    {"level", level},
                    {"state", 1},
                    {"value", value},
                    {"triggered_at", now},
                };
                active_[key] = curr;
            } else if (level != curr.level) {
                it->second.level = level;
                it->second.value = value;
                triggered = {
                    {"device_code", deviceCode},
                    {"point_address", pointAddress},
                    {"point_name", pointName},
                    {"level", level},
                    {"state", 1},
                    {"value", value},
                    {"triggered_at", it->second.triggered_at},
                };
            }
        } else if (has && curr.state == 1) {
            active_.erase(it);
            triggered = {
                {"device_code", deviceCode},
                {"point_address", pointAddress},
                {"point_name", pointName},
                {"state", 3},
                {"recovered_at", now},
            };
        }
        return triggered;
    }

  private:
    std::unordered_map<std::string, AlarmState> active_;
};

}  // namespace

SamplePump::SamplePump(drogon::orm::DbClientPtr db, const std::string &copyDsn,
                       CollectorBridge &collector, ws::WsHub &hub, int intervalMs)
    : db_(std::move(db)), copyDsn_(copyDsn), collector_(collector), hub_(hub), intervalMs_(intervalMs) {}

SamplePump::~SamplePump() {
    stop();
    if (copyConn_) PQfinish(copyConn_);
}

void SamplePump::start() {
    if (running_.exchange(true)) return;
    worker_ = std::thread([this] { run(); });
    LOG_INFO << "SamplePump started interval_ms=" << intervalMs_;
}

void SamplePump::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    LOG_INFO << "SamplePump stopped";
}

void SamplePump::warmupPointCache() {
    auto r = db_->execSqlSync(
        "SELECT id, using_point_id, device_code, item_name, address, "
        "alarm_high, alarm_low, alarm_enabled, scale, \"offset\" "
        "FROM using_points WHERE enabled = true AND using_point_id IS NOT NULL");
    std::unordered_map<std::string, PointMeta> cache;
    for (const auto &row : r) {
        if (row["using_point_id"].isNull()) continue;
        PointMeta m;
        m.id = row["id"].as<int>();
        m.device_code = row["device_code"].as<std::string>();
        m.item_name = row["item_name"].isNull() ? "" : row["item_name"].as<std::string>();
        m.address = row["address"].isNull() ? "" : row["address"].as<std::string>();
        m.alarm_high = row["alarm_high"].isNull() ? 0.0 : row["alarm_high"].as<double>();
        m.alarm_low = row["alarm_low"].isNull() ? 0.0 : row["alarm_low"].as<double>();
        m.alarm_enabled = !row["alarm_enabled"].isNull() && row["alarm_enabled"].as<bool>();
        m.scale = row["scale"].isNull() ? 1.0 : row["scale"].as<double>();
        m.offset = row["offset"].isNull() ? 0.0 : row["offset"].as<double>();
        cache[std::to_string(row["using_point_id"].as<int64_t>())] = m;
    }
    pointCache_ = std::move(cache);
    LOG_INFO << "point cache warmed count=" << pointCache_.size();
}

bool SamplePump::ensureCopyConn() {
    if (copyConn_ && PQstatus(copyConn_) == CONNECTION_OK) return true;
    if (copyConn_) {
        PQfinish(copyConn_);
        copyConn_ = nullptr;
    }
    copyConn_ = PQconnectdb(copyDsn_.c_str());
    if (!copyConn_ || PQstatus(copyConn_) != CONNECTION_OK) {
        LOG_ERROR << "SamplePump copy connect failed: "
                  << (copyConn_ ? PQerrorMessage(copyConn_) : "null conn");
        if (copyConn_) {
            PQfinish(copyConn_);
            copyConn_ = nullptr;
        }
        return false;
    }
    return true;
}

void SamplePump::batchCopyTs(const std::vector<std::tuple<std::string, int64_t, double, std::string, std::string>> &rows) {
    if (!ensureCopyConn()) {
        fallbackInsertTs(rows);
        return;
    }
    PGresult *res = PQexec(copyConn_,
        "COPY time_series_data (ts, point_id, value, quality, raw_value) FROM STDIN");
    if (PQresultStatus(res) != PGRES_COPY_IN) {
        LOG_ERROR << "COPY begin failed: " << PQerrorMessage(copyConn_);
        PQclear(res);
        fallbackInsertTs(rows);
        return;
    }
    PQclear(res);
    std::string buf;
    buf.reserve(rows.size() * 64);
    for (const auto &[ts, pid, val, qual, raw] : rows) {
        buf += ts;
        buf += '\t';
        buf += std::to_string(pid);
        buf += '\t';
        buf += fmtDouble(val);
        buf += '\t';
        buf += escapeCopy(qual);
        buf += '\t';
        if (raw.empty()) buf += "\\N";
        else buf += escapeCopy(raw);
        buf += '\n';
    }
    if (PQputCopyData(copyConn_, buf.data(), static_cast<int>(buf.size())) != 1) {
        LOG_ERROR << "PQputCopyData failed: " << PQerrorMessage(copyConn_);
        PQputCopyEnd(copyConn_, "abort");
        PQclear(PQgetResult(copyConn_));
        fallbackInsertTs(rows);
        return;
    }
    if (PQputCopyEnd(copyConn_, nullptr) != 1) {
        LOG_ERROR << "PQputCopyEnd failed: " << PQerrorMessage(copyConn_);
        PQclear(PQgetResult(copyConn_));
        fallbackInsertTs(rows);
        return;
    }
    PGresult *end = PQgetResult(copyConn_);
    if (end) {
        if (PQresultStatus(end) != PGRES_COMMAND_OK) {
            LOG_ERROR << "COPY commit failed: " << PQresultErrorMessage(end);
            PQclear(end);
            fallbackInsertTs(rows);
            return;
        }
        PQclear(end);
    }
}

void SamplePump::fallbackInsertTs(const std::vector<std::tuple<std::string, int64_t, double, std::string, std::string>> &rows) {
    if (rows.empty()) return;
    std::string sql = "INSERT INTO time_series_data (ts, point_id, value, quality, raw_value) VALUES ";
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i) sql += ",";
        sql += "($" + std::to_string(i * 5 + 1) + ",$" + std::to_string(i * 5 + 2)
             + ",$" + std::to_string(i * 5 + 3) + ",$" + std::to_string(i * 5 + 4)
             + ",$" + std::to_string(i * 5 + 5) + ")";
    }
    sql += " ON CONFLICT DO NOTHING";
    // Drogon execSqlSync 支持变长参数；这里逐行执行更稳妥
    try {
        for (const auto &[ts, pid, val, qual, raw] : rows) {
            std::string tsV = ts;
            std::string qualV = qual;
            std::string rawV = raw.empty() ? std::string() : raw;
            db_->execSqlSync(
                "INSERT INTO time_series_data (ts, point_id, value, quality, raw_value) "
                "VALUES ($1,$2,$3,$4,$5) ON CONFLICT DO NOTHING",
                tsV, pid, val, qualV, rawV);
        }
    } catch (const std::exception &e) {
        LOG_ERROR << "fallback insert ts failed: " << e.what();
    }
}

std::vector<nlohmann::json> SamplePump::evaluateAlarms(const std::vector<Sample> &samples, int64_t now) {
    static ActiveAlarmTracker tracker;
    std::vector<nlohmann::json> events;
    for (const auto &s : samples) {
        auto it = pointCache_.find(std::to_string(s.point_id));
        if (it == pointCache_.end()) continue;
        const auto &meta = it->second;
        auto ev = tracker.checkAndDecide(meta.device_code, meta.address, meta.item_name,
                                         s.value, s.quality.empty() ? "Good" : s.quality,
                                         meta.alarm_high, meta.alarm_low, meta.alarm_enabled, now);
        if (!ev.is_null()) events.push_back(std::move(ev));
    }
    return events;
}

void SamplePump::persistAlarms(const std::vector<nlohmann::json> &events) {
    for (const auto &ev : events) {
        try {
            if (ev.value("state", 0) == 3) {
                db_->execSqlSync(
                    "UPDATE using_alarms SET state = 3, recovered_at = $1 "
                    "WHERE device_code = $2 AND point_address = $3 AND state = 1",
                    static_cast<int64_t>(ev["recovered_at"].get<int64_t>()),
                    ev["device_code"].get<std::string>(),
                    ev["point_address"].get<std::string>());
            } else {
                db_->execSqlSync(
                    "INSERT INTO using_alarms (device_code, point_address, point_name, level, state, message, value, triggered_at) "
                    "VALUES ($1,$2,$3,$4,$5,$6,$7,$8) ON CONFLICT DO NOTHING",
                    ev["device_code"].get<std::string>(),
                    ev["point_address"].get<std::string>(),
                    ev.value("point_name", ""),
                    static_cast<int16_t>(ev.value("level", 2)),
                    static_cast<int16_t>(1), "",
                    ev.value("value", 0.0),
                    static_cast<int64_t>(ev["triggered_at"].get<int64_t>()));
            }
        } catch (const std::exception &e) {
            LOG_ERROR << "persist alarm failed: " << e.what();
        }
    }
}

void SamplePump::run() {
    warmupPointCache();
    while (running_) {
        try {
            tick();
        } catch (const std::exception &e) {
            LOG_ERROR << "SamplePump.tick error: " << e.what();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs_));
    }
}

void SamplePump::tick() {
    auto [newSeq, samples] = collector_.snapshotSince(seq_);
    seq_ = newSeq;
    if (samples.empty()) return;

    bool unknown = false;
    for (const auto &s : samples) {
        if (pointCache_.find(std::to_string(s.point_id)) == pointCache_.end()) {
            unknown = true;
            break;
        }
    }
    if (unknown) warmupPointCache();

    int64_t now = std::time(nullptr);

    std::vector<std::tuple<std::string, int64_t, double, std::string, std::string>> tsRows;
    std::set<std::pair<std::string, int>> seen;
    for (const auto &s : samples) {
        auto it = pointCache_.find(std::to_string(s.point_id));
        if (it == pointCache_.end()) continue;
        int64_t tsMs = s.ts_ms != 0 ? s.ts_ms : now * 1000;
        std::string ts = pgTs(tsMs);
        auto key = std::make_pair(ts, it->second.id);
        if (!seen.insert(key).second) continue;
        tsRows.emplace_back(ts, it->second.id, s.value,
                            s.quality.empty() ? "Good" : s.quality, s.raw_value);
    }
    if (!tsRows.empty()) batchCopyTs(tsRows);

    auto alarmEvents = evaluateAlarms(samples, now);
    if (!alarmEvents.empty()) {
        persistAlarms(alarmEvents);
        Json::Value arr(Json::arrayValue);
        for (auto &e : alarmEvents) arr.append(util::fromNlohmann(e));
        Json::Value payload(Json::objectValue);
        payload["type"] = "alarm";
        payload["events"] = arr;
        hub_.broadcast("alarm", payload);
    }

    Json::Value sampleArr(Json::arrayValue);
    for (const auto &s : samples) {
        Json::Value o(Json::objectValue);
        o["device_code"] = s.device_key;
        o["point_id"] = static_cast<Json::Int64>(s.point_id);
        o["value"] = s.value;
        o["quality"] = s.quality.empty() ? "Good" : s.quality;
        sampleArr.append(o);
    }
    Json::Value dataPayload(Json::objectValue);
    dataPayload["type"] = "data";
    dataPayload["ts"] = static_cast<Json::Int64>(now);
    dataPayload["count"] = static_cast<int>(samples.size());
    dataPayload["samples"] = sampleArr;
    hub_.broadcast("data", dataPayload);
}

}  // namespace ibmcp