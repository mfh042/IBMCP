#pragma once

#include <string>

namespace ibmcp {

// Modbus 地址解析结果（纯函数，不依赖 libmodbus，便于单元测试）。
// register_index 为 0-based（对齐 libmodbus modbus_read_registers 的 addr 参数）。
// Modicon 记法：<area>x<offset>，area 取值 0/1/3/4；
//   0 = 线圈(Coil)            -> 功能码 01 读 / 05 写
//   1 = 离散输入(Discrete)    -> 功能码 02 读（只读）
//   3 = 输入寄存器(Input Reg) -> 功能码 04 读（只读）
//   4 = 保持寄存器(Holding)   -> 功能码 03 读 / 06 写
// offset 为 1-based，解析后减 1 得到 0-based register_index。
// 无 area 前缀的纯数字默认按保持寄存器（area 4）处理，兼容不规范地址。
struct ModbusAddress {
    int function_code = 0;  // 01/02/03/04，0 表示无效
    int register_index = 0; // 0-based
    bool valid = false;
};

ModbusAddress parse_modbus_address(const std::string& addr);

// 给定 data_type，读取该点位需要的寄存器数量（float=2，其余=1）。
int modbus_register_count(const std::string& data_type);

} // namespace ibmcp