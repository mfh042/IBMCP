#pragma once

#include <memory>
#include <string>

#include "driver.h"

namespace ibmcp {

// 协议名归一化：与 backend/app/services/plan_compiler.py 的 normalize_protocol 逐字对齐。
// 先转小写，再剔除字符集 { '_', '-', ' ', '.' }。
// MODBUS_TCP / Modbus TCP / modbus-tcp / modbus.tcp -> "modbustcp"
std::string normalize_protocol(const std::string& name);

// 按归一化后的协议名创建驱动实例。协议不支持或依赖缺失时返回 nullptr
// （调用方负责回退到模拟值生成，行为对齐 stub）。
std::unique_ptr<IDriver> create_driver(const std::string& protocol);

// 当前编译目标支持的协议列表（受 IBMCP_HAS_MODBUS 等宏控制）。
std::vector<std::string> available_driver_protocols();

} // namespace ibmcp