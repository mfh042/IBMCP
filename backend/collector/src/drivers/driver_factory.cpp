#include "driver_factory.h"
#include "modbus_tcp_driver.h"
#include "modbus_rtu_driver.h"
#include "mqtt_driver.h"
#include "opcua_driver.h"

#include <cctype>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ibmcp {

namespace {

using FactoryFn = std::function<std::unique_ptr<IDriver>()>;

std::map<std::string, FactoryFn>& registry() {
    static std::map<std::string, FactoryFn> reg;
    if (reg.empty()) {
        // 无条件注册：无对应系统库时驱动走退化分支（connect 返回 false），
        // 由上层回退模拟值，行为对齐 stub。真实能力由 available_driver_protocols 按宏报告。
        reg.emplace("modbustcp", [] { return std::make_unique<ModbusTcpDriver>(); });
        reg.emplace("modbusrtu", [] { return std::make_unique<ModbusRtuDriver>(); });
        reg.emplace("mqtt", [] { return std::make_unique<MqttDriver>(); });
        reg.emplace("opcua", [] { return std::make_unique<OpcUaDriver>(); });
    }
    return reg;
}

} // namespace

std::string normalize_protocol(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        char lc = static_cast<char>(std::tolower(c));
        if (lc == '_' || lc == '-' || lc == ' ' || lc == '.') continue;
        out.push_back(lc);
    }
    return out;
}

std::unique_ptr<IDriver> create_driver(const std::string& protocol) {
    const std::string key = normalize_protocol(protocol);
    auto& reg = registry();
    auto it = reg.find(key);
    if (it == reg.end()) return nullptr;
    return it->second();
}

std::vector<std::string> available_driver_protocols() {
    std::vector<std::string> out;
#ifdef IBMCP_HAS_MODBUS
    out.push_back("modbustcp");
    out.push_back("modbusrtu");
#endif
#ifdef IBMCP_HAS_MQTT
    out.push_back("mqtt");
#endif
#ifdef IBMCP_HAS_OPCUA
    out.push_back("opcua");
#endif
    return out;
}

} // namespace ibmcp