#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace ibmcp {

// 线程安全的事件分发器。事件载体为 map<string,string>（与绑定层回调签名一致）。
// emit 时拷贝回调后再调用，消费者抛异常则吞掉（对齐 stub _emit_event 的 try/except），
// 避免单个事件消费者的异常拖垮采集主循环或 API 调用线程。
class EventEmitter {
public:
    using Event = std::map<std::string, std::string>;
    using Callback = std::function<void(const Event&)>;

    void set_callback(Callback cb);
    void clear_callback();
    void emit(const Event& evt);

private:
    mutable std::mutex mtx_;
    Callback cb_;
};

} // namespace ibmcp