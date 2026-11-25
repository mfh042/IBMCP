#include "control/command_waiter.h"

namespace ibmcp::control {

CommandWaiter &CommandWaiter::instance() {
    static CommandWaiter w;
    return w;
}

std::string CommandWaiter::key(const std::string &deviceKey, int pointId) {
    return deviceKey + ":" + std::to_string(pointId);
}

std::shared_future<nlohmann::json> CommandWaiter::expect(const std::string &deviceKey, int pointId) {
    auto promise = std::make_shared<std::promise<nlohmann::json>>();
    auto fut = promise->get_future().share();
    std::lock_guard<std::mutex> lk(mu_);
    pending_[key(deviceKey, pointId)] = std::move(promise);
    return fut;
}

bool CommandWaiter::resolve(const std::string &deviceKey, int pointId, const nlohmann::json &result) {
    std::shared_ptr<std::promise<nlohmann::json>> p;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = pending_.find(key(deviceKey, pointId));
        if (it == pending_.end()) return false;
        p = std::move(it->second);
        pending_.erase(it);
    }
    p->set_value(result);
    return true;
}

size_t CommandWaiter::pending() const {
    std::lock_guard<std::mutex> lk(mu_);
    return pending_.size();
}

}  // namespace ibmcp::control