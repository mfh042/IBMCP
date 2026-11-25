#pragma once
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ibmcp {

struct Point {
    int point_id = 0;
    std::string point_key;
    std::string item_code;
    std::string item_name;
    std::string address;
    std::string data_type = "float";
    std::string read_write = "R";
    int cycle_ms = 1000;
    double scale = 1.0;
    double offset = 0.0;
    double alarm_high = 0.0;
    double alarm_low = 0.0;
    bool alarm_enabled = false;
    std::string unit;
};

struct Device {
    std::string device_key;
    std::string device_name;
    std::string equipment_code;
    std::string equipment_name;
    std::string route_code;
    std::string step_code;
    std::string protocol;
    nlohmann::json connection;
    int connect_timeout_ms = 3000;
    int read_timeout_ms = 1000;
    std::vector<Point> points;
};

struct Plan {
    int revision = 0;
    int flush_interval_ms = 200;
    std::string plan_hash;
    std::vector<Device> devices;
};

// 解析 nlohmann::json（绑定层 _py_to_json 产出）为内部 Plan 结构。
// 精确对齐 backend/app/services/plan_compiler.py 输出的 schema；
// 结构损坏或必填字段缺失时抛 InvalidConnection（rc=2），与 stub 校验错误码一致。
Plan parse_plan(const nlohmann::json& plan_json);

// 协议白名单单一真源（与 backend stub 的 SUPPORTED_PROTOCOLS 逐字对齐），
// list_protocols / protocol_capabilities / validate_plan 共用，杜绝漂移。
const std::vector<std::string>& supported_protocols();

// 语义校验：协议白名单、address 非空、scale≠0。
// 精确复刻 backend/app/core/collector/stub.py 的 _validate_plan，
// 抛对应 CollectorError 子类（rc 与 stage 与 stub 一致），失败时调用方不更新 current_plan。
void validate_plan(const Plan& plan);

} // namespace ibmcp