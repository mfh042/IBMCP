#include "data_store.h"

#include <string>

namespace ibmcp {

DataStore::DataStore() : ring_(kCapacity) {}
DataStore::~DataStore() = default;

void DataStore::push(const Sample& sample) {
    std::lock_guard<std::mutex> lk(mtx_);
    Sample s = sample;
    s.seq = ++seq_;
    ring_[head_] = std::move(s);
    head_ = (head_ + 1) % kCapacity;
    if (count_ < kCapacity) {
        ++count_;
    } else {
        ++dropped_;
    }
}

std::pair<int, std::vector<std::map<std::string, std::string>>>
DataStore::snapshot_since(int since_seq) {
    std::vector<std::map<std::string, std::string>> out;
    std::lock_guard<std::mutex> lk(mtx_);
    out.reserve(count_);
    const size_t start = (head_ + kCapacity - count_) % kCapacity;
    for (size_t i = 0; i < count_; ++i) {
        const Sample& s = ring_[(start + i) % kCapacity];
        if (s.seq > since_seq) {
            out.push_back({
                {"seq", std::to_string(s.seq)},
                {"device_key", s.device_key},
                {"point_id", std::to_string(s.point_id)},
                {"value", s.value},
                {"quality", s.quality},
                {"ts_ms", std::to_string(s.ts_ms)},
            });
        }
    }
    return {static_cast<int>(seq_), std::move(out)};
}

size_t DataStore::size() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return count_;
}

int64_t DataStore::dropped() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return dropped_;
}

int64_t DataStore::max_seq() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return seq_;
}

void DataStore::clear() {
    std::lock_guard<std::mutex> lk(mtx_);
    head_ = 0;
    count_ = 0;
    seq_ = 0;
    dropped_ = 0;
}

} // namespace ibmcp