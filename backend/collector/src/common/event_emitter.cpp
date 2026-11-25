#include "event_emitter.h"

namespace ibmcp {

void EventEmitter::set_callback(Callback cb) {
    std::lock_guard<std::mutex> lk(mtx_);
    cb_ = std::move(cb);
}

void EventEmitter::clear_callback() {
    std::lock_guard<std::mutex> lk(mtx_);
    cb_ = nullptr;
}

void EventEmitter::emit(const Event& evt) {
    Callback cb;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        cb = cb_;
    }
    if (!cb) return;
    try {
        cb(evt);
    } catch (...) {
        // 对齐 stub：消费者异常不影响采集器，静默丢弃。
    }
}

} // namespace ibmcp