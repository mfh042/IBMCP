#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace ibmcp {

// 固定大小 IO 线程池：submit 的任务在工作线程并发执行。
// stop() 会排空队列中已提交的任务再退出（drain），避免丢读。
// 线程安全：start/stop/submit 可从任意线程调用。
class IoPool {
public:
    // n_threads==0 时用默认值：max(1, min(hardware_concurrency(), 8))。
    explicit IoPool(size_t n_threads = 0);
    ~IoPool();

    IoPool(const IoPool&) = delete;
    IoPool& operator=(const IoPool&) = delete;

    void start();
    void stop();

    // 提交任务。pool 未启动或已停止时静默丢弃。
    void submit(std::function<void()> task);

    size_t size() const { return threads_.size(); }

private:
    void worker();

    size_t n_threads_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::vector<std::thread> threads_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> tasks_;
};

} // namespace ibmcp