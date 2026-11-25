#pragma once

#include <cstdint>
#include <random>
#include <string>

#include "plan/plan.h"

namespace ibmcp {

// 生成与 backend/app/core/collector/stub.py 逐行对齐的模拟采样值
// （正弦趋势 + 噪声 + 偶发越限），用于驱动不可用/未连接时的回退，
// 保证无硬件环境下前端曲线与 stub 行为完全一致、不退化。
std::string make_sim_value(const Point& pt, int64_t t_ms, std::mt19937& rng);

} // namespace ibmcp