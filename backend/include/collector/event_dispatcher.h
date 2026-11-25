#pragma once

#include <drogon/orm/DbClient.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include "collector/collector_bridge.h"
#include "control/command_waiter.h"
#include "ws/ws_hub.h"

namespace ibmcp {

class EventDispatcher {
  public:
    EventDispatcher(drogon::orm::DbClientPtr db, CollectorBridge &collector,
                    ws::WsHub &hub, control::CommandWaiter &waiter);
    ~EventDispatcher();
    void start();
    void stop();

  private:
    void onEvent(const nlohmann::json &evt);
    void run();
    void handle(const nlohmann::json &evt);
    void onDeviceStatus(const nlohmann::json &evt);
    void onWriteResult(const nlohmann::json &evt);
    void onPlanAck(const nlohmann::json &evt);
    static int64_t eventSeconds(const nlohmann::json &evt);

    drogon::orm::DbClientPtr db_;
    CollectorBridge &collector_;
    ws::WsHub &hub_;
    control::CommandWaiter &waiter_;

    std::queue<nlohmann::json> queue_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

}  // namespace ibmcp