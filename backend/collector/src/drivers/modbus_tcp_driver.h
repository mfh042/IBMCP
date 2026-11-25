#pragma once

#include "driver.h"

namespace ibmcp {

// Modbus TCP 驱动。一个实例对应一台设备。
// 编译时若未定义 IBMCP_HAS_MODBUS（欧拉未装 libmodbus-devel），所有方法退化为失败，
// 由上层回退到模拟值生成，行为不退化（对齐 stub 曲线）。
class ModbusTcpDriver : public IDriver {
public:
    ModbusTcpDriver();
    ~ModbusTcpDriver() override;

    std::string protocol() const override { return "modbustcp"; }

    bool connect(const nlohmann::json& connection,
                 int connect_timeout_ms, int read_timeout_ms) override;
    std::vector<ReadResult> read_batch(const std::vector<Point>& points) override;
    bool write(const Point& point, const std::string& value) override;
    void disconnect() override;

private:
    void* ctx_ = nullptr; // 实为 modbus_t*，头文件不引入 libmodbus 头（其 modbus_t 是 typedef，无法前置声明）
    bool connected_ = false;
};

} // namespace ibmcp