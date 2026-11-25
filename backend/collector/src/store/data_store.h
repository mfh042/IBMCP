#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ibmcp {

// 一条采样记录。value 存字符串：调度器读出原始值并应用 scale/offset 后自行格式化，
// 存储层不假设数值类型（也支持非数值点位），与 collector.h 的 map<string,string> 返回一致。
struct Sample {
    int64_t seq = 0;
    std::string device_key;
    int point_id = 0;
    std::string value;
    std::string quality = "Good";
    int64_t ts_ms = 0;
};

// 线程安全的定长环形缓冲（容量对齐 backend stub 的 deque(maxlen=10000)）。
// 多生产者（IO 线程池）push，单消费者（snapshot_since 调用线程）取。
// 当前用互斥锁保证正确性；热点瓶颈出现后可替换为 MPSC 无锁环形缓冲。
class DataStore {
public:
    static constexpr size_t kCapacity = 10000;

    DataStore();
    ~DataStore();

    // 写入一条采样，自动分配单调递增的 seq；缓冲满时覆盖最旧记录并累计 dropped。
    void push(const Sample& sample);

    // 返回 seq > since_seq 的全部采样（按 seq 升序），以及当前最大 seq。
    std::pair<int, std::vector<std::map<std::string, std::string>>>
    snapshot_since(int since_seq);

    size_t size() const;     // 当前缓冲中的样本数
    int64_t dropped() const; // 累计因覆盖丢弃的样本数
    int64_t max_seq() const;
    void clear();

private:
    mutable std::mutex mtx_;
    std::vector<Sample> ring_;
    size_t head_ = 0;   // 下一个写入位置
    size_t count_ = 0;  // 当前有效元素数（<= kCapacity）
    int64_t seq_ = 0;   // 已分配的最大 seq
    int64_t dropped_ = 0;
};

} // namespace ibmcp