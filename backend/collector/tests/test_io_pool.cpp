#include <gtest/gtest.h>

#include "scheduler/io_pool.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace ibmcp;

TEST(IoPool, ExecutesSubmittedTasks) {
    IoPool pool(2);
    pool.start();
    std::atomic<int> count{0};
    for (int i = 0; i < 20; ++i) {
        pool.submit([&] { ++count; });
    }
    pool.stop();
    EXPECT_EQ(count.load(), 20);
}

TEST(IoPool, TasksRunConcurrently) {
    IoPool pool(4);
    pool.start();
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 4; ++i) {
        pool.submit([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        });
    }
    pool.stop();
    auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();
    // 4 个各 sleep 100ms 的任务并发执行应远小于串行的 400ms。
    EXPECT_LT(dt, 300);
}

TEST(IoPool, StopDrainsQueuedTasks) {
    IoPool pool(1);
    pool.start();
    std::atomic<bool> done{false};
    pool.submit([&] { done.store(true); });
    pool.stop(); // 立即停止应先排空已提交任务
    EXPECT_TRUE(done.load());
}