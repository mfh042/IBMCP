#pragma once

#include "driver.h"

namespace ibmcp {

// Modbus RTU 驱动（串口）。一个实例对应一台设备。
// 与 ModbusTcpDriver 共用 libmodbus 的寄存器编解码逻辑，仅连接上下文不同
// （modbus_new_rtu 串口）。编译时若未定义 IBMCP_HAS_MODBUS，退化为失败，
// 由上层回退模拟值，行为对齐 stub。
class ModbusRtuDriver : public IDriver {
public:
    ModbusRtuDriver();
    ~ModbusRtuDriver() override;

    std::string protocol() const override { return "modbusrtu"; }

    bool connect(const nlohmann::json& connection,
                 int connect_timeout_ms, int read_timeout_ms) override;
    std::vector<ReadResult> read_batch(const std::vector<Point>& points) override;
    bool write(const Point& point, const std::string& value) override;
    void disconnect() override;

private:
    void* ctx_ = nullptr; // 实为 modbus_t*
    bool connected_ = false;
};

} // namespace ibmcp