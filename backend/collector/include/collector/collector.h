#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>

#include <nlohmann/json.hpp>

namespace ibmcp {

class Collector {
public:
    static Collector& instance();
    int init(const std::map<std::string, std::string>& cfg);
    std::map<std::string, std::string> health() const;
    std::vector<std::string> list_protocols() const;
    std::map<std::string, std::map<std::string, int>> protocol_capabilities() const;
    int apply_plan(const nlohmann::json& plan, int revision);
    void set_event_callback(std::function<void(const std::map<std::string, std::string>&)> cb);
    std::pair<int, std::vector<std::map<std::string, std::string>>> snapshot_since(int since_seq);
    int write_point(const std::string& device_key, int point_id, const std::string& value);
    void shutdown();
private:
    Collector();
    ~Collector();
    Collector(const Collector&) = delete;
    Collector& operator=(const Collector&) = delete;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ibmcp