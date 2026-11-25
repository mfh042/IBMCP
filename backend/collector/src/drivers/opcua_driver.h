#pragma once

#include "driver.h"

#include <string>

namespace ibmcp {

// OPC UA 驱动（open62541 客户端）。一个实例对应一台设备。
// address 语义：OPC UA NodeId 字符串，如 "ns=3;s=Temperature1"，直接用
// UA_NodeId_parse 解析。read_batch 逐节点读 Value 属性；write 按 data_type 编码写入。
// 编译时若未定义 IBMCP_HAS_OPCUA，退化为失败，由上层回退模拟值（对齐 stub）。
class OpcUaDriver : public IDriver {
public:
    OpcUaDriver();
    ~OpcUaDriver() override;

    std::string protocol() const override { return "opcua"; }

    bool connect(const nlohmann::json& connection,
                 int connect_timeout_ms, int read_timeout_ms) override;
    std::vector<ReadResult> read_batch(const std::vector<Point>& points) override;
    bool write(const Point& point, const std::string& value) override;
    void disconnect() override;

private:
    void* client_ = nullptr; // 实为 UA_Client*
    bool connected_ = false;
};

} // namespace ibmcp