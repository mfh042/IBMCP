#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ibmcp {

struct Sample {
    int64_t seq = 0;
    int64_t point_id = 0;
    int64_t ts_ms = 0;
    double value = 0.0;
    std::string quality;
    std::string raw_value;
    std::string device_key;
};

struct HealthInfo {
    int threads = 0;
    int revision = 0;
    int device_count = 0;
    int devices_real = 0;
    int devices_degraded = 0;
    int point_count = 0;
    int queue_depth = 0;
    int dropped = 0;
    int uptime_sec = 0;
    std::vector<std::string> protocols;
};

class CollectorBridge {
  public:
    void init(const nlohmann::json &cfg);
    HealthInfo health();
    std::vector<std::string> protocols();
    nlohmann::json capabilities();
    int applyPlan(const nlohmann::json &plan, int revision);
    std::pair<int64_t, std::vector<Sample>> snapshotSince(int64_t sinceSeq);
    int writePoint(const std::string &deviceKey, int pointId, const std::string &value);
    void setEventCallback(std::function<void(const nlohmann::json &)> cb);
    void shutdown();

  private:
    static std::string toStubProtocol(const std::string &name);
    static bool parseBool(const std::string &v);
    static int64_t parseInt(const std::string &v);
    static double parseDouble(const std::string &v);
    static nlohmann::json normalizeEvent(const std::map<std::string, std::string> &evt);

    std::function<void(const nlohmann::json &)> userCb_;
};

}  // namespace ibmcp