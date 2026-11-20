#pragma once
#include <stdexcept>
#include <string>

namespace ibmcp {

class CollectorError : public std::runtime_error {
public:
    CollectorError(int rc, std::string stage, std::string device_key,
                   int64_t point_id, std::string msg)
        : std::runtime_error(std::move(msg)), rc_(rc), stage_(std::move(stage)),
          device_key_(std::move(device_key)), point_id_(point_id) {}

    int rc() const noexcept { return rc_; }
    const std::string& stage() const noexcept { return stage_; }
    const std::string& device_key() const noexcept { return device_key_; }
    int64_t point_id() const noexcept { return point_id_; }

private:
    int rc_;
    std::string stage_;
    std::string device_key_;
    int64_t point_id_;
};

class ProtocolNotSupported  : public CollectorError { using CollectorError::CollectorError; };
class InvalidConnection     : public CollectorError { using CollectorError::CollectorError; };
class PlanRevisionConflict  : public CollectorError { using CollectorError::CollectorError; };
class NotInitialized        : public CollectorError { using CollectorError::CollectorError; };
class ControlQueueFull     : public CollectorError { using CollectorError::CollectorError; };
class DeviceNotInPlan       : public CollectorError { using CollectorError::CollectorError; };

} // namespace ibmcp