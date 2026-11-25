#include "plan.h"
#include "collector/exports.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ibmcp {

const std::vector<std::string>& supported_protocols() {
    static const std::vector<std::string> protos = {
        "modbus_tcp", "modbus_rtu", "opc_ua", "mqtt",
        "socket_tcp", "socket_udp", "websocket",
    };
    return protos;
}

void validate_plan(const Plan& plan) {
    const auto& protos = supported_protocols();
    for (const auto& dev : plan.devices) {
        // 与 stub 一致：空 protocol 跳过不报错（plan_compiler 已保证非空）
        if (!dev.protocol.empty() &&
            std::find(protos.begin(), protos.end(), dev.protocol) == protos.end()) {
            throw ProtocolNotSupported(1, "apply_plan.validate", dev.device_key, -1,
                                       "protocol not supported: " + dev.protocol);
        }
        for (const auto& pt : dev.points) {
            if (pt.address.empty()) {
                throw InvalidConnection(2, "apply_plan.validate", dev.device_key, pt.point_id,
                                        "point address is empty (point_id=" +
                                        std::to_string(pt.point_id) + ")");
            }
            if (pt.scale == 0.0) {
                throw InvalidConnection(2, "apply_plan.validate", dev.device_key, pt.point_id,
                                        "scale must not be 0 (point_id=" +
                                        std::to_string(pt.point_id) + ")");
            }
        }
    }
}

} // namespace ibmcp