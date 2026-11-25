#pragma once

#include <drogon/orm/DbClient.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>

#include "collector/collector_bridge.h"
#include "ws/ws_hub.h"

typedef struct pg_conn PGconn;

namespace ibmcp {

struct PointMeta {
    int id = 0;
    std::string device_code;
    std::string item_name;
    std::string address;
    double alarm_high = 0.0;
    double alarm_low = 0.0;
    bool alarm_enabled = false;
    double scale = 1.0;
    double offset = 0.0;
};

class SamplePump {
  public:
    SamplePump(drogon::orm::DbClientPtr db, const std::string &copyDsn,
               CollectorBridge &collector, ws::WsHub &hub, int intervalMs = 200);
    ~SamplePump();
    void start();
    void stop();

  private:
    void run();
    void tick();
    void warmupPointCache();
    bool ensureCopyConn();
    void batchCopyTs(const std::vector<std::tuple<std::string, int64_t, double, std::string, std::string>> &rows);
    void fallbackInsertTs(const std::vector<std::tuple<std::string, int64_t, double, std::string, std::string>> &rows);
    std::vector<nlohmann::json> evaluateAlarms(const std::vector<Sample> &samples, int64_t now);
    void persistAlarms(const std::vector<nlohmann::json> &events);

    drogon::orm::DbClientPtr db_;
    std::string copyDsn_;
    CollectorBridge &collector_;
    ws::WsHub &hub_;
    int intervalMs_;
    int64_t seq_{0};
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::unordered_map<std::string, PointMeta> pointCache_;
    PGconn *copyConn_{nullptr};
};

}  // namespace ibmcp