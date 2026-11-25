#include "controller/ws_push_controller.h"

#include <json/reader.h>
#include <json/writer.h>

#include "ws/ws_hub.h"

namespace ibmcp {

namespace {

std::string toJsonStr(const Json::Value &v) {
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    return Json::writeString(b, v);
}

void sendJson(const drogon::WebSocketConnectionPtr &ws, const Json::Value &v) {
    std::string s = toJsonStr(v);
    ws->send(s.data(), s.size(), drogon::WebSocketMessageType::Text);
}

}  // namespace

void WsPushController::handleNewConnection(const drogon::HttpRequestPtr &,
                                           const drogon::WebSocketConnectionPtr &ws) {
    std::string id = ws::WsHub::instance().connect(ws);
    std::lock_guard<std::mutex> lk(mu_);
    connToId_[ws] = id;
}

std::string WsPushController::idOf(const drogon::WebSocketConnectionPtr &ws) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = connToId_.find(ws);
    return it == connToId_.end() ? std::string() : it->second;
}

void WsPushController::handleNewMessage(const drogon::WebSocketConnectionPtr &ws,
                                        std::string &&message,
                                        const drogon::WebSocketMessageType &) {
    Json::CharReaderBuilder b;
    Json::Value msg;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    if (!reader->parse(message.data(), message.data() + message.size(), &msg, &errs)) return;

    std::string id = idOf(ws);
    if (id.empty()) return;
    std::string type = msg.get("type", "").asString();
    std::string topic = msg.get("topic", "").asString();
    if (type == "subscribe" && !topic.empty()) {
        ws::WsHub::instance().subscribe(id, topic);
        Json::Value r(Json::objectValue);
        r["type"] = "subscribed";
        r["topic"] = topic;
        sendJson(ws, r);
    } else if (type == "unsubscribe" && !topic.empty()) {
        ws::WsHub::instance().unsubscribe(id, topic);
    } else if (type == "ping") {
        Json::Value r(Json::objectValue);
        r["type"] = "pong";
        sendJson(ws, r);
    }
}

void WsPushController::handleConnectionClosed(const drogon::WebSocketConnectionPtr &ws) {
    std::string id;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = connToId_.find(ws);
        if (it != connToId_.end()) {
            id = it->second;
            connToId_.erase(it);
        }
    }
    if (!id.empty()) ws::WsHub::instance().disconnect(id);
}

}  // namespace ibmcp