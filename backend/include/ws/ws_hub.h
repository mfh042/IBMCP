#pragma once

#include <drogon/WebSocketConnection.h>
#include <drogon/nosql/RedisClient.h>
#include <drogon/nosql/RedisSubscriber.h>
#include <json/json.h>

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

namespace ibmcp::ws {

class WsHub {
  public:
    static WsHub &instance();

    void init();

    std::string connect(const drogon::WebSocketConnectionPtr &ws);
    void disconnect(const std::string &clientId);
    void subscribe(const std::string &clientId, const std::string &topic);
    void unsubscribe(const std::string &clientId, const std::string &topic);
    void broadcast(const std::string &topic, const Json::Value &payload);

  private:
    WsHub() = default;
    bool validTopic(const std::string &t) const;
    void deliverLocal(const std::string &topic, const std::string &msg);

    std::mutex mu_;
    std::map<std::string, drogon::WebSocketConnectionPtr> clients_;
    std::map<std::string, std::set<std::string>> topics_;

    bool initialized_{false};
    drogon::nosql::RedisClientPtr redis_;
    std::shared_ptr<drogon::nosql::RedisSubscriber> sub_;
};

}  // namespace ibmcp::ws
