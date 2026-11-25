#pragma once

#include "driver.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>

struct mosquitto;
struct mosquitto_message;

namespace ibmcp {

// MQTT 驱动（libmosquitto）。一个实例对应一台设备。
// address 语义：MQTT topic 字符串。read_batch 返回各 topic 最近收到的消息值，
// 按消息新鲜度（max_message_age_ms）判定 Good / Stale / Bad。write 向 topic 发布。
// 编译时若未定义 IBMCP_HAS_MQTT，退化为失败，由上层回退模拟值（对齐 stub）。
class MqttDriver : public IDriver {
public:
    MqttDriver();
    ~MqttDriver() override;

    std::string protocol() const override { return "mqtt"; }

    bool connect(const nlohmann::json& connection,
                 int connect_timeout_ms, int read_timeout_ms) override;
    std::vector<ReadResult> read_batch(const std::vector<Point>& points) override;
    bool write(const Point& point, const std::string& value) override;
    void disconnect() override;

private:
    static void on_message(struct mosquitto* mosq, void* obj, const struct mosquitto_message* msg);

    void* mosq_ = nullptr; // 实为 struct mosquitto*
    bool connected_ = false;
    int qos_ = 1;
    int max_age_ms_ = 30000;

    std::mutex mu_;
    // topic -> (payload, timestamp_ms)
    std::map<std::string, std::pair<std::string, uint64_t>> latest_;
    std::set<std::string> subscribed_; // 已订阅过的 topic（实例级，每台设备独立）
};

} // namespace ibmcp