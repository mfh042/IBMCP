#include "io_pool.h"

namespace ibmcp {

IoPool::IoPool(size_t n_threads) {
    if (n_threads == 0) {
        unsigned hw = std::thread::hardware_concurrency();
        n_threads = (hw == 0) ? 2u : hw;
        if (n_threads > 8) n_threads = 8;
    }
    n_threads_ = n_threads;
}

IoPool::~IoPool() { stop(); }

void IoPool::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    stop_.store(false);
    for (size_t i = 0; i < n_threads_; ++i) {
        threads_.emplace_back([this] { worker(); });
    }
}

void IoPool::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) return;
    stop_.store(true);
    cv_.notify_all();
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
}

void IoPool::submit(std::function<void()> task) {
    if (!task) return;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!running_.load()) return; // 未启动或已停止，丢弃
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
}

void IoPool::worker() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this] { return stop_.load() || !tasks_.empty(); });
            if (tasks_.empty()) return; // stop 且队列已排空
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        try {
            task();
        } catch (...) {
            // 吞掉任务异常，避免工作线程崩溃拖垮整个池。
        }
    }
}

} // namespace ibmcp