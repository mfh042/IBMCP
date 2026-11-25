#include "collector/event_dispatcher.h"

#include "util/json_util.h"

#include <drogon/drogon.h>

#include <optional>

namespace ibmcp {

EventDispatcher::EventDispatcher(drogon::orm::DbClientPtr db, CollectorBridge &collector,
                                 ws::WsHub &hub, control::CommandWaiter &waiter)
    : db_(std::move(db)), collector_(collector), hub_(hub), waiter_(waiter) {}

EventDispatcher::~EventDispatcher() { stop(); }

void EventDispatcher::start() {
    if (running_.exchange(true)) return;
    worker_ = std::thread([this] { run(); });
    collector_.setEventCallback([this](const nlohmann::json &e) { onEvent(e); });
    LOG_INFO << "EventDispatcher started";
}

void EventDispatcher::stop() {
    if (!running_.exchange(false)) return;
    try {
        collector_.setEventCallback(nullptr);
    } catch (...) {
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    LOG_INFO << "EventDispatcher stopped";
}

void EventDispatcher::onEvent(const nlohmann::json &evt) {
    std::lock_guard<std::mutex> lk(mu_);
    if (queue_.size() >= 10000) {
        LOG_WARN << "event queue full, dropped event type=" << evt.value("type", "");
        return;
    }
    queue_.push(evt);
    cv_.notify_one();
}

void EventDispatcher::run() {
    while (running_) {
        nlohmann::json evt;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] { return !queue_.empty() || !running_; });
            if (!running_ && queue_.empty()) break;
            evt = std::move(queue_.front());
            queue_.pop();
        }
        try {
            handle(evt);
        } catch (const std::exception &e) {
            LOG_ERROR << "event dispatch failed: " << e.what();
        }
    }
}

int64_t EventDispatcher::eventSeconds(const nlohmann::json &evt) {
    if (evt.contains("ts_ms") && evt["ts_ms"].is_number()) {
        return evt["ts_ms"].get<int64_t>() / 1000;
    }
    for (const char *k : {"finished_at", "ts"}) {
        if (evt.contains(k) && evt[k].is_number()) return evt[k].get<int64_t>();
    }
    return std::time(nullptr);
}

void EventDispatcher::handle(const nlohmann::json &evt) {
    std::string type = evt.value("type", "");
    if (type == "device_status") onDeviceStatus(evt);
    else if (type == "write_result") onWriteResult(evt);
    else if (type == "plan_ack") onPlanAck(evt);
    else if (type == "collector_stats") LOG_DEBUG << "collector_stats";
    else LOG_DEBUG << "unknown collector event type=" << type;
}

void EventDispatcher::onDeviceStatus(const nlohmann::json &evt) {
    std::string deviceKey = evt.value("device_key", "");
    if (deviceKey.empty()) return;
    int16_t status = static_cast<int16_t>(evt.value("status", 0));
    std::string reason = evt.value("reason", "");
    int64_t ts = eventSeconds(evt);

    db_->execSqlSync(
        "INSERT INTO using_devices (device_code, device_name, status, updated_at) "
        "VALUES ($1, $1, $2, $3) "
        "ON CONFLICT (device_code) DO UPDATE SET status = EXCLUDED.status, "
        "updated_at = EXCLUDED.updated_at",
        deviceKey, status, static_cast<int64_t>(ts));
    db_->execSqlSync(
        "INSERT INTO using_device_runtime (device_key, status, last_error, snapshot_at) "
        "VALUES ($1, $2, $3, $4) "
        "ON CONFLICT (device_key) DO UPDATE SET status = EXCLUDED.status, "
        "last_error = EXCLUDED.last_error, snapshot_at = EXCLUDED.snapshot_at",
        deviceKey, status,
        reason.empty() ? std::optional<std::string>{} : std::optional<std::string>{reason},
        static_cast<int64_t>(ts));

    Json::Value payload(Json::objectValue);
    payload["type"] = "device_status";
    payload["device_code"] = deviceKey;
    payload["status"] = status;
    payload["reason"] = reason;
    payload["ts"] = static_cast<Json::Int64>(ts);
    hub_.broadcast("device", payload);
}

void EventDispatcher::onWriteResult(const nlohmann::json &evt) {
    if (!evt.contains("command_id")) return;
    int64_t commandId = evt["command_id"].get<int64_t>();
    bool success = evt.value("success", false);
    int16_t status = success ? static_cast<int16_t>(2) : static_cast<int16_t>(3);
    std::string message = evt.value("message", success ? "" : "write failed");
    int64_t ts = eventSeconds(evt);

    db_->execSqlSync(
        "UPDATE control_commands SET status = $2, message = $3, finished_at = $4 "
        "WHERE ext_command_id = $1",
        commandId, status, message, static_cast<int64_t>(ts));

    Json::Value payload(Json::objectValue);
    payload["type"] = "control_finished";
    payload["command_id"] = static_cast<Json::Int64>(commandId);
    payload["device_code"] = evt.value("device_key", "");
    payload["point_id"] = static_cast<Json::Int64>(evt.value("point_id", 0));
    payload["success"] = success;
    payload["status"] = status;
    payload["message"] = message;
    payload["ts"] = static_cast<Json::Int64>(ts);
    hub_.broadcast("control", payload);

    if (evt.contains("device_key") && evt.contains("point_id")) {
        nlohmann::json r;
        r["command_id"] = commandId;
        r["success"] = success;
        r["status"] = status;
        r["message"] = message;
        r["elapsed_ms"] = evt.value("elapsed_ms", 0);
        waiter_.resolve(evt["device_key"].get<std::string>(),
                        evt["point_id"].get<int>(), r);
    }
}

void EventDispatcher::onPlanAck(const nlohmann::json &evt) {
    if (!evt.contains("revision")) return;
    int64_t revision = evt["revision"].get<int64_t>();
    bool ok = false;
    if (evt.contains("status")) {
        const auto &raw = evt["status"];
        if (raw.is_number_integer()) ok = (raw.get<int64_t>() == 0);
        else if (raw.is_string()) {
            auto s = raw.get<std::string>();
            ok = (s == "ok" || s == "success");
        } else if (raw.is_boolean()) ok = raw.get<bool>();
    }
    int16_t status = ok ? static_cast<int16_t>(1) : static_cast<int16_t>(2);
    std::string error = evt.value("error", "");
    int64_t ts = eventSeconds(evt);
    db_->execSqlSync(
        "UPDATE runtime_plan_revision SET status = $2, extension_error = $3, applied_at = $4 "
        "WHERE revision = $1",
        revision, status,
        error.empty() ? std::optional<std::string>{} : std::optional<std::string>{error},
        static_cast<int64_t>(ts));
    if (!ok) LOG_ERROR << "plan_ack failed revision=" << revision << " error=" << error;
}

}  // namespace ibmcp