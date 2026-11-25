#include "ws/ws_hub.h"

#include <json/value.h>
#include <json/writer.h>

#include <random>

#include <drogon/drogon.h>

#include "db/redis.h"

namespace ibmcp::ws {

namespace {
constexpr const char *kChannelPrefix = "ibmcp:ws:";
constexpr size_t kChannelPrefixLen = 9;  // strlen("ibmcp:ws:")
}  // namespace

WsHub &WsHub::instance() {
    static WsHub hub;
    return hub;
}

bool WsHub::validTopic(const std::string &t) const {
    return t == "data" || t == "device" || t == "alarm" || t == "control";
}

void WsHub::init() {
    if (initialized_) return;
    initialized_ = true;
    redis_ = ibmcp::redis::client();
    if (!redis_) {
        LOG_ERROR << "WS hub init skipped: redis client not available";
        return;
    }
    try {
        sub_ = redis_->newSubscriber();
        sub_->psubscribe(
            "ibmcp:ws:*",
            [this](const std::string &channel, const std::string &message) {
                if (channel.size() <= kChannelPrefixLen) return;
                std::string topic = channel.substr(kChannelPrefixLen);
                deliverLocal(topic, message);
            });
        LOG_INFO << "WS hub subscribed to ibmcp:ws:*";
    } catch (const std::exception &e) {
        LOG_ERROR << "WS hub init failed: " << e.what();
    }
}

std::string WsHub::connect(const drogon::WebSocketConnectionPtr &ws) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<int64_t> dist;
    std::string id = std::to_string(dist(gen));
    std::lock_guard<std::mutex> lk(mu_);
    clients_[id] = ws;
    return id;
}

void WsHub::disconnect(const std::string &clientId) {
    std::lock_guard<std::mutex> lk(mu_);
    clients_.erase(clientId);
    for (auto &[topic, set] : topics_) set.erase(clientId);
}

void WsHub::subscribe(const std::string &clientId, const std::string &topic) {
    if (!validTopic(topic)) return;
    std::lock_guard<std::mutex> lk(mu_);
    topics_[topic].insert(clientId);
}

void WsHub::unsubscribe(const std::string &clientId, const std::string &topic) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = topics_.find(topic);
    if (it != topics_.end()) it->second.erase(clientId);
}

void WsHub::broadcast(const std::string &topic, const Json::Value &payload) {
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    std::string msg = Json::writeString(b, payload);

    if (!redis_) {
        LOG_ERROR << "WS broadcast skipped: redis not initialized, topic=" << topic;
        return;
    }
    std::string channel = std::string(kChannelPrefix) + topic;
    redis_->execCommandAsync(
        [](const drogon::nosql::RedisResult &) {},
        [topic](const drogon::nosql::RedisException &e) {
            LOG_ERROR << "WS publish failed topic=" << topic << " err=" << e.what();
        },
        "PUBLISH %s %s", channel.c_str(), msg.c_str());
}

void WsHub::deliverLocal(const std::string &topic, const std::string &msg) {
    std::vector<drogon::WebSocketConnectionPtr> targets;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = topics_.find(topic);
        if (it == topics_.end()) return;
        std::vector<std::string> dead;
        for (const auto &cid : it->second) {
            auto cit = clients_.find(cid);
            if (cit == clients_.end()) {
                dead.push_back(cid);
                continue;
            }
            targets.push_back(cit->second);
        }
        for (const auto &cid : dead) it->second.erase(cid);
    }
    for (auto &ws : targets) {
        try {
            ws->send(msg.data(), msg.size(), drogon::WebSocketMessageType::Text);
        } catch (...) {
        }
    }
}

}  // namespace ibmcp::ws
