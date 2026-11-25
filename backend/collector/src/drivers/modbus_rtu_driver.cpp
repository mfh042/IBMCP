#include "modbus_rtu_driver.h"
#include "modbus_address.h"

#include <cstring>
#include <cstdio>
#include <cctype>
#include <string>
#include <vector>

#ifdef IBMCP_HAS_MODBUS
#include <modbus/modbus.h>
#endif

namespace ibmcp {

ModbusRtuDriver::ModbusRtuDriver() = default;
ModbusRtuDriver::~ModbusRtuDriver() { disconnect(); }

#ifdef IBMCP_HAS_MODBUS

bool ModbusRtuDriver::connect(const nlohmann::json& connection,
                              int connect_timeout_ms, int read_timeout_ms) {
    disconnect();
    if (!connection.contains("device") || !connection["device"].is_string()) {
        return false;
    }
    std::string device = connection["device"].get<std::string>();
    int baud = 9600;
    char parity = 'N';
    int data_bits = 8;
    int stop_bits = 1;
    int slave = 1;
    if (connection.contains("baud")) baud = connection["baud"].get<int>();
    if (connection.contains("parity")) {
        std::string p = connection["parity"].get<std::string>();
        if (!p.empty()) parity = static_cast<char>(std::toupper(static_cast<unsigned char>(p[0])));
    }
    if (connection.contains("data_bits")) data_bits = connection["data_bits"].get<int>();
    if (connection.contains("stop_bits")) stop_bits = connection["stop_bits"].get<int>();
    // 兼容文档契约 "slave" 与历史 "slave_id" 两种键名。
    if (connection.contains("slave")) slave = connection["slave"].get<int>();
    else if (connection.contains("slave_id")) slave = connection["slave_id"].get<int>();

    modbus_t* ctx = modbus_new_rtu(device.c_str(), baud, parity, data_bits, stop_bits);
    ctx_ = ctx;
    if (!ctx) return false;
    modbus_set_error_recovery(ctx, static_cast<modbus_error_recovery_mode>(
        MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL));
    modbus_set_slave(ctx, slave);
    uint32_t to_sec = connect_timeout_ms / 1000;
    uint32_t to_usec = (connect_timeout_ms % 1000) * 1000;
    modbus_set_response_timeout(ctx, to_sec, to_usec);
    if (modbus_connect(ctx) == -1) {
        modbus_free(ctx);
        ctx_ = nullptr;
        return false;
    }
    connected_ = true;
    (void)read_timeout_ms;
    return true;
}

std::vector<ReadResult> ModbusRtuDriver::read_batch(const std::vector<Point>& points) {
    std::vector<ReadResult> out;
    out.reserve(points.size());
    if (!connected_ || !ctx_) {
        for (const auto& pt : points) {
            out.push_back({pt.point_id, "", "Bad", "not connected"});
        }
        return out;
    }

    std::vector<ModbusAddress> addrs;
    addrs.reserve(points.size());
    int min_idx = -1;
    int max_idx = -1;
    for (const auto& pt : points) {
        ModbusAddress a = parse_modbus_address(pt.address);
        addrs.push_back(a);
        if (!a.valid) continue;
        int cnt = modbus_register_count(pt.data_type);
        int end = a.register_index + cnt - 1;
        if (min_idx < 0 || a.register_index < min_idx) min_idx = a.register_index;
        if (end > max_idx) max_idx = end;
    }

    modbus_t* ctx = static_cast<modbus_t*>(ctx_);
    std::vector<uint16_t> buf;
    bool range_ok = false;
    if (min_idx >= 0 && max_idx >= min_idx) {
        int nb = max_idx - min_idx + 1;
        buf.resize(nb);
        int rc = modbus_read_registers(ctx, min_idx, nb, buf.data());
        range_ok = (rc == nb);
    }

    for (size_t i = 0; i < points.size(); ++i) {
        const auto& pt = points[i];
        const ModbusAddress& a = addrs[i];
        ReadResult r;
        r.point_id = pt.point_id;
        if (!a.valid) {
            r.quality = "Bad";
            r.error = "invalid address: " + pt.address;
            out.push_back(std::move(r));
            continue;
        }
        int pos = a.register_index - min_idx;
        int cnt = modbus_register_count(pt.data_type);
        if (!range_ok || pos < 0 || pos + cnt > static_cast<int>(buf.size())) {
            r.quality = "Bad";
            r.error = "read failed";
            out.push_back(std::move(r));
            continue;
        }
        if (pt.data_type == "bool") {
            r.value = (buf[pos] != 0) ? "1" : "0";
        } else if (pt.data_type == "float" || pt.data_type == "double") {
            uint32_t raw = (static_cast<uint32_t>(buf[pos]) << 16) |
                            static_cast<uint32_t>(buf[pos + 1]);
            float f;
            std::memcpy(&f, &raw, sizeof(f));
            double v = static_cast<double>(f) * pt.scale + pt.offset;
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "%.3f", v);
            r.value = tmp;
        } else {
            double v = static_cast<double>(static_cast<int16_t>(buf[pos])) * pt.scale + pt.offset;
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "%.3f", v);
            r.value = tmp;
        }
        r.quality = "Good";
        out.push_back(std::move(r));
    }
    return out;
}

bool ModbusRtuDriver::write(const Point& point, const std::string& value) {
    if (!connected_ || !ctx_) return false;
    modbus_t* ctx = static_cast<modbus_t*>(ctx_);
    ModbusAddress a = parse_modbus_address(point.address);
    if (!a.valid) return false;
    try {
        double dval = std::stod(value);
        int cnt = modbus_register_count(point.data_type);
        if (cnt >= 2) {
            float f = static_cast<float>(dval);
            uint32_t raw;
            std::memcpy(&raw, &f, sizeof(raw));
            uint16_t regs[2];
            regs[0] = static_cast<uint16_t>((raw >> 16) & 0xFFFF);
            regs[1] = static_cast<uint16_t>(raw & 0xFFFF);
            return modbus_write_registers(ctx, a.register_index, 2, regs) == 2;
        }
        uint16_t reg = static_cast<uint16_t>(static_cast<int>(dval) & 0xFFFF);
        return modbus_write_register(ctx, a.register_index, reg) == 1;
    } catch (...) {
        return false;
    }
}

void ModbusRtuDriver::disconnect() {
    if (ctx_) {
        modbus_t* ctx = static_cast<modbus_t*>(ctx_);
        modbus_close(ctx);
        modbus_free(ctx);
        ctx_ = nullptr;
    }
    connected_ = false;
}

#else

bool ModbusRtuDriver::connect(const nlohmann::json&, int, int) { return false; }
std::vector<ReadResult> ModbusRtuDriver::read_batch(const std::vector<Point>& points) {
    std::vector<ReadResult> out;
    for (const auto& pt : points) out.push_back({pt.point_id, "", "Bad", "modbus unavailable"});
    return out;
}
bool ModbusRtuDriver::write(const Point&, const std::string&) { return false; }
void ModbusRtuDriver::disconnect() {}

#endif

} // namespace ibmcp