#pragma once

#include <drogon/WebSocketController.h>

#include <map>
#include <mutex>
#include <string>

namespace ibmcp {

class WsPushController : public drogon::WebSocketController<WsPushController> {
  public:
    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws/push");
    WS_PATH_ADD("/api/v1/ws/collect");
    WS_PATH_LIST_END

    void handleNewConnection(const drogon::HttpRequestPtr &,
                             const drogon::WebSocketConnectionPtr &ws) override;
    void handleNewMessage(const drogon::WebSocketConnectionPtr &ws, std::string &&message,
                          const drogon::WebSocketMessageType &type) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr &ws) override;

  private:
    std::string idOf(const drogon::WebSocketConnectionPtr &ws);

    std::mutex mu_;
    std::map<drogon::WebSocketConnectionPtr, std::string> connToId_;
};

}  // namespace ibmcp