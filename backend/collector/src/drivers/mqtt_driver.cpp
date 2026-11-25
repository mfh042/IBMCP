#include "mqtt_driver.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#ifdef IBMCP_HAS_MQTT
#if __has_include(<mosquitto.h>)
#include <mosquitto.h>
#elif __has_include(<mosquitto/mosquitto.h>)
#include <mosquitto/mosquitto.h>
#endif
#endif

namespace ibmcp {

MqttDriver::MqttDriver() = default;
MqttDriver::~MqttDriver() { disconnect(); }

#ifdef IBMCP_HAS_MQTT

namespace {
uint64_t now_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
} // namespace

void MqttDriver::on_message(struct mosquitto*, void* obj, const struct mosquitto_message* msg) {
    auto* self = static_cast<MqttDriver*>(obj);
    if (!self || !msg || !msg->topic) return;
    std::string topic(msg->topic);
    std::string payload;
    if (msg->payload && msg->payloadlen > 0) {
        payload.assign(static_cast<const char*>(msg->payload), static_cast<size_t>(msg->payloadlen));
    }
    std::lock_guard<std::mutex> lk(self->mu_);
    self->latest_[topic] = std::make_pair(std::move(payload), now_ms());
}

bool MqttDriver::connect(const nlohmann::json& connection,
                         int connect_timeout_ms, int read_timeout_ms) {
    disconnect();
    if (!connection.contains("host") || !connection["host"].is_string()) return false;
    if (!connection.contains("port") || !connection["port"].is_number()) return false;
    std::string host = connection["host"].get<std::string>();
    int port = connection["port"].get<int>();

    std::string client_id;
    if (connection.contains("client_id") && connection["client_id"].is_string()) {
        client_id = connection["client_id"].get<std::string>();
    }
    if (client_id.empty()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "ibmcp-%llu",
                      static_cast<unsigned long long>(now_ms()));
        client_id = buf;
    }
    int keep_alive = 60;
    if (connection.contains("keep_alive")) keep_alive = connection["keep_alive"].get<int>();
    qos_ = 1;
    if (connection.contains("qos")) qos_ = connection["qos"].get<int>();
    max_age_ms_ = 30000;
    if (connection.contains("max_message_age_ms")) {
        max_age_ms_ = connection["max_message_age_ms"].get<int>();
    }

    mosquitto_lib_init();
    struct mosquitto* mosq = mosquitto_new(client_id.c_str(), true, this);
    if (!mosq) return false;
    mosq_ = mosq;

    if (connection.contains("username") && connection["username"].is_string()) {
        std::string u = connection["username"].get<std::string>();
        std::string p;
        if (connection.contains("password") && connection["password"].is_string()) {
            p = connection["password"].get<std::string>();
        }
        mosquitto_username_pw_set(mosq, u.c_str(), p.empty() ? nullptr : p.c_str());
    }
    mosquitto_message_callback_set(mosq, &MqttDriver::on_message);

    // connect 阻塞：受 connect_timeout_ms 约束（mosquitto 内部默认超时，这里传 0 让其用默认）
    int rc = mosquitto_connect(mosq, host.c_str(), port, keep_alive);
    if (rc != MOSQ_ERR_SUCCESS) {
        mosquitto_destroy(mosq);
        mosq_ = nullptr;
        return false;
    }
    rc = mosquitto_loop_start(mosq);
    if (rc != MOSQ_ERR_SUCCESS) {
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
        mosq_ = nullptr;
        return false;
    }
    connected_ = true;
    (void)connect_timeout_ms;
    (void)read_timeout_ms;
    return true;
}

std::vector<ReadResult> MqttDriver::read_batch(const std::vector<Point>& points) {
    std::vector<ReadResult> out;
    out.reserve(points.size());
    if (!connected_ || !mosq_) {
        for (const auto& pt : points) {
            out.push_back({pt.point_id, "", "Bad", "not connected"});
        }
        return out;
    }
    struct mosquitto* mosq = static_cast<struct mosquitto*>(mosq_);
    uint64_t now = now_ms();

    // 先收集需要新订阅的 topic（不持锁，避免阻塞网络线程）
    std::vector<std::string> to_subscribe;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (const auto& pt : points) {
            if (subscribed_.find(pt.address) == subscribed_.end()) {
                to_subscribe.push_back(pt.address);
                subscribed_.insert(pt.address);
            }
        }
    }
    for (const auto& topic : to_subscribe) {
        int mid = 0;
        mosquitto_subscribe(mosq, &mid, topic.c_str(), qos_);
    }

    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& pt : points) {
        ReadResult r;
        r.point_id = pt.point_id;
        auto it = latest_.find(pt.address);
        if (it == latest_.end()) {
            r.quality = "Bad";
            r.error = "no message";
            out.push_back(std::move(r));
            continue;
        }
        uint64_t age = now - it->second.second;
        const std::string& payload = it->second.first;
        if (pt.data_type == "string") {
            r.value = payload;
        } else if (pt.data_type == "bool") {
            bool truthy = !payload.empty() && payload != "0" &&
                          payload != "false" && payload != "False" && payload != "FALSE";
            r.value = truthy ? "1" : "0";
        } else {
            try {
                double v = std::stod(payload) * pt.scale + pt.offset;
                char tmp[32];
                std::snprintf(tmp, sizeof(tmp), "%.3f", v);
                r.value = tmp;
            } catch (...) {
                r.quality = "Bad";
                r.error = "parse failed: " + payload;
                out.push_back(std::move(r));
                continue;
            }
        }
        r.quality = (age <= static_cast<uint64_t>(max_age_ms_)) ? "Good" : "Stale";
        out.push_back(std::move(r));
    }
    return out;
}

bool MqttDriver::write(const Point& point, const std::string& value) {
    if (!connected_ || !mosq_) return false;
    struct mosquitto* mosq = static_cast<struct mosquitto*>(mosq_);
    int mid = 0;
    int rc = mosquitto_publish(mosq, &mid, point.address.c_str(),
                               static_cast<int>(value.size()),
                               value.data(), qos_, false);
    return rc == MOSQ_ERR_SUCCESS;
}

void MqttDriver::disconnect() {
    if (mosq_) {
        struct mosquitto* mosq = static_cast<struct mosquitto*>(mosq_);
        mosquitto_loop_stop(mosq, false);
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
        mosq_ = nullptr;
    }
    connected_ = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        latest_.clear();
        subscribed_.clear();
    }
}

#else

bool MqttDriver::connect(const nlohmann::json&, int, int) { return false; }
std::vector<ReadResult> MqttDriver::read_batch(const std::vector<Point>& points) {
    std::vector<ReadResult> out;
    for (const auto& pt : points) out.push_back({pt.point_id, "", "Bad", "mqtt unavailable"});
    return out;
}
bool MqttDriver::write(const Point&, const std::string&) { return false; }
void MqttDriver::disconnect() {}

#endif

} // namespace ibmcp