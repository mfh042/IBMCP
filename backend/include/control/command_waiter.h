#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ibmcp::control {

class CommandWaiter {
  public:
    static CommandWaiter &instance();

    std::shared_future<nlohmann::json> expect(const std::string &deviceKey, int pointId);
    bool resolve(const std::string &deviceKey, int pointId, const nlohmann::json &result);
    size_t pending() const;

  private:
    CommandWaiter() = default;
    static std::string key(const std::string &deviceKey, int pointId);

    mutable std::mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<std::promise<nlohmann::json>>> pending_;
};

}  // namespace ibmcp::control