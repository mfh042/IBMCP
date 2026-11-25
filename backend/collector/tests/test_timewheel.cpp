#include <gtest/gtest.h>

#include "scheduler/timewheel.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace ibmcp;

TEST(TimeWheel, PeriodicTimerFiresMultipleTimes) {
    TimeWheel wheel;
    std::atomic<int> count{0};
    wheel.start();
    wheel.add_periodic(50, [&] { ++count; });
    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    wheel.stop();
    EXPECT_GE(count.load(), 3);
}

TEST(TimeWheel, CancelStopsFurtherFiring) {
    TimeWheel wheel;
    std::atomic<int> count{0};
    wheel.start();
    auto id = wheel.add_periodic(50, [&] { ++count; });
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    wheel.cancel(id);
    std::this_thread::sleep_for(std::chrono::milliseconds(70)); // 等在途回调完成
    int after = count.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    wheel.stop();
    EXPECT_EQ(count.load(), after);
}

TEST(TimeWheel, StopIsIdempotentAndFast) {
    TimeWheel wheel;
    wheel.start();
    wheel.add_periodic(10000, [] {});
    auto t0 = std::chrono::steady_clock::now();
    wheel.stop();
    wheel.stop();
    auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();
    EXPECT_LT(dt, 200);
}