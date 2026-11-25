#include "modbus_address.h"

#include <cctype>

namespace ibmcp {

namespace {

bool parse_int(const std::string& s, int& out) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    try {
        out = std::stoi(s);
    } catch (...) {
        return false;
    }
    return true;
}

} // namespace

ModbusAddress parse_modbus_address(const std::string& addr) {
    ModbusAddress r;
    if (addr.empty()) return r;

    std::string s = addr;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    if (s.empty()) return r;

    int area = 4; // 默认保持寄存器
    std::string num_part = s;

    // 识别 "<digit>x<number>" 或 "<digit><number>" 前缀
    if (s.size() >= 2 && (s[0] == '0' || s[0] == '1' || s[0] == '3' || s[0] == '4')) {
        if (s[1] == 'x' || s[1] == 'X') {
            area = s[0] - '0';
            num_part = s.substr(2);
        }
    }

    int offset = 0;
    if (!parse_int(num_part, offset) || offset < 1) return r;

    switch (area) {
        case 0: r.function_code = 1; break;
        case 1: r.function_code = 2; break;
        case 3: r.function_code = 4; break;
        case 4: r.function_code = 3; break;
        default: return r;
    }
    r.register_index = offset - 1; // 1-based -> 0-based（libmodbus 约定）
    r.valid = true;
    return r;
}

int modbus_register_count(const std::string& data_type) {
    if (data_type == "float" || data_type == "double") return 2;
    return 1;
}

} // namespace ibmcp