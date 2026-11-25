#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "plan/plan.h"

namespace ibmcp {

// 单次点位读取结果。value 存字符串（与 Sample.value / 绑定层 map<string,string> 一致），
// quality 取值对齐 stub："Good" / "Uncertain" / "Bad"；读取失败时 error 非空、value 留空。
struct ReadResult {
    int point_id = 0;
    std::string value;
    std::string quality = "Good";
    std::string error;
};

// 协议驱动抽象接口。一个 driver 实例对应一台设备（持有连接上下文）。
// 调度器负责按 cycle_ms 触发 read_batch；write 用于控制下发。
class IDriver {
public:
    virtual ~IDriver() = default;

    virtual std::string protocol() const = 0;

    // 建立连接。connection 来自 plan 的 devices[].connection（结构因协议而异）。
    // 失败返回 false，不抛异常（错误信息由调用方记入 device_status 事件）。
    virtual bool connect(const nlohmann::json& connection,
                         int connect_timeout_ms, int read_timeout_ms) = 0;

    // 批量读取本设备全部点位。实现应尽量合并寄存器区间减少 IO 次数。
    virtual std::vector<ReadResult> read_batch(const std::vector<Point>& points) = 0;

    // 单点控制写入。成功返回 true。
    virtual bool write(const Point& point, const std::string& value) = 0;

    virtual void disconnect() = 0;
};

} // namespace ibmcp