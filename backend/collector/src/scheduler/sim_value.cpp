#include "sim_value.h"

#include <cmath>
#include <cstdio>
#include <cctype>
#include <string>

namespace ibmcp {
namespace {

constexpr double kPi2 = 6.2831853;

struct SignalRange {
    double center;
    double amp;
    double ah;
    double al;
};

std::string lower_copy(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

// 与 backend/app/core/collector/stub.py 的 _signal_range 逐行对齐。
SignalRange signal_range(const Point& pt) {
    double ah = pt.alarm_high;
    double al = pt.alarm_low;
    if (ah > 0) {
        double lo = ah * 0.2;
        double hi = ah * 0.95;
        return {(lo + hi) / 2.0, (hi - lo) / 2.0, ah, al};
    }
    if (al < 0) {
        double lo = al * 0.95;
        double hi = al * 0.2;
        return {(lo + hi) / 2.0, (hi - lo) / 2.0, ah, al};
    }
    if (al > 0) {
        double lo = al * 1.05;
        double hi = al * 1.9;
        return {(lo + hi) / 2.0, (hi - lo) / 2.0, ah, al};
    }
    const std::string u = lower_copy(pt.unit);
    const std::string it = lower_copy(pt.item_code.empty() ? pt.item_name : pt.item_code);
    struct Hint { const char* keys[3]; double center; double amp; };
    static const Hint hints[] = {
        {{"temp", "\xc2\xb0" "c", "\xe2\x84\x83"}, 50.0, 15.0},
        {{"pa", "mpa", "bar"}, 0.6, 0.2},
        {{"v", "\xe7\x94\xb5\xe5\x8e\x8b", ""}, 220.0, 10.0},
        {{"a", "\xe7\x94\xb5\xe6\xb5\x81", ""}, 10.0, 2.0},
        {{"w", "kw", "\xe5\x8a\x9f\xe7\x8e\x87"}, 15.0, 5.0},
        {{"%", "percent", ""}, 60.0, 15.0},
        {{"hz", "\xe9\xa2\x91\xe7\x8e\x87", ""}, 50.0, 2.0},
        {{"rpm", "r/min", "\xe8\xbd\xac\xe9\x80\x9f"}, 1500.0, 300.0},
    };
    for (const auto& h : hints) {
        for (int i = 0; i < 3; ++i) {
            if (h.keys[i][0] == '\0') continue;
            std::string k = h.keys[i];
            if (u.find(k) != std::string::npos || it.find(k) != std::string::npos) {
                return {h.center, h.amp, 0.0, 0.0};
            }
        }
    }
    return {50.0, 15.0, 0.0, 0.0};
}

} // namespace

// 与 stub.py 的 _make_value 逐行对齐，round(val, 3) 用 snprintf %.3f。
std::string make_sim_value(const Point& pt, int64_t t_ms, std::mt19937& rng) {
    SignalRange sr = signal_range(pt);
    int pid = pt.point_id;
    double phase = std::fmod(pid * 0.7, kPi2);
    double period = 45000.0 + (pid % 5) * 15000.0;
    double base = sr.center + sr.amp * std::sin(static_cast<double>(t_ms) / period * kPi2 + phase);
    std::uniform_real_distribution<double> noise_dist(-sr.amp * 0.12, sr.amp * 0.12);
    double val = base + noise_dist(rng);
    std::uniform_real_distribution<double> u01(0.0, 1.0);
    if (sr.ah > 0 && u01(rng) < 0.03) {
        std::uniform_real_distribution<double> d(0.01, 0.08);
        val = sr.ah + sr.ah * d(rng);
    } else if (sr.al != 0 && u01(rng) < 0.02) {
        std::uniform_real_distribution<double> d(0.01, 0.05);
        val = sr.al - std::abs(sr.al) * d(rng);
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", val);
    return buf;
}

} // namespace ibmcp