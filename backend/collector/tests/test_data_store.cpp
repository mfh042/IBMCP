#include <gtest/gtest.h>

#include <atomic>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "store/data_store.h"

using namespace ibmcp;

namespace {

Sample mk(const std::string& dk, int pid, const std::string& val, int64_t ts) {
    Sample s;
    s.device_key = dk;
    s.point_id = pid;
    s.value = val;
    s.ts_ms = ts;
    return s;
}

} // namespace

TEST(DataStore, EmptySnapshot) {
    DataStore ds;
    auto [seq, samples] = ds.snapshot_since(0);
    EXPECT_EQ(seq, 0);
    EXPECT_TRUE(samples.empty());
    EXPECT_EQ(ds.size(), 0u);
    EXPECT_EQ(ds.dropped(), 0);
}

TEST(DataStore, IncrementalFetch) {
    DataStore ds;
    ds.push(mk("D1", 1, "1.5", 1000));
    ds.push(mk("D1", 2, "2.5", 1001));
    ds.push(mk("D2", 3, "3.5", 1002));

    EXPECT_EQ(ds.max_seq(), 3);
    EXPECT_EQ(ds.size(), 3u);

    auto [s0, all] = ds.snapshot_since(0);
    EXPECT_EQ(s0, 3);
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0]["seq"], "1");
    EXPECT_EQ(all[1]["seq"], "2");
    EXPECT_EQ(all[2]["seq"], "3");

    auto [s1, rest] = ds.snapshot_since(1);
    ASSERT_EQ(rest.size(), 2u);
    EXPECT_EQ(rest[0]["seq"], "2");
    EXPECT_EQ(rest[1]["seq"], "3");

    auto [s3, none] = ds.snapshot_since(3);
    EXPECT_TRUE(none.empty());
}

TEST(DataStore, FieldRoundtrip) {
    DataStore ds;
    Sample s = mk("CNC-01", 42, "123.456", 1700000000000LL);
    s.quality = "Uncertain";
    ds.push(s);

    auto [_, samples] = ds.snapshot_since(0);
    ASSERT_EQ(samples.size(), 1u);
    const auto& m = samples[0];
    EXPECT_EQ(m.at("device_key"), "CNC-01");
    EXPECT_EQ(m.at("point_id"), "42");
    EXPECT_EQ(m.at("value"), "123.456");
    EXPECT_EQ(m.at("quality"), "Uncertain");
    EXPECT_EQ(m.at("ts_ms"), "1700000000000");
}

TEST(DataStore, OverwriteDropsOld) {
    DataStore ds;
    const size_t cap = DataStore::kCapacity;
    for (int i = 0; i < static_cast<int>(cap) + 5; ++i) {
        ds.push(mk("D", i, std::to_string(i), i));
    }
    EXPECT_EQ(ds.size(), cap);
    EXPECT_EQ(ds.dropped(), 5);
    EXPECT_EQ(ds.max_seq(), static_cast<int64_t>(cap) + 5);

    auto [seq, samples] = ds.snapshot_since(0);
    ASSERT_EQ(samples.size(), cap);
    // 最旧的 5 条被覆盖，剩余 seq 应从 6 开始连续到 cap+5
    EXPECT_EQ(samples.front()["seq"], "6");
    EXPECT_EQ(samples.back()["seq"], std::to_string(cap + 5));
}

TEST(DataStore, ClearResets) {
    DataStore ds;
    ds.push(mk("D", 1, "1", 1));
    ds.push(mk("D", 2, "2", 2));
    ds.clear();
    EXPECT_EQ(ds.size(), 0u);
    EXPECT_EQ(ds.dropped(), 0);
    EXPECT_EQ(ds.max_seq(), 0);
    auto [seq, samples] = ds.snapshot_since(0);
    EXPECT_EQ(seq, 0);
    EXPECT_TRUE(samples.empty());
}

TEST(DataStore, ThreadSafety) {
    DataStore ds;
    const int kThreads = 4;
    const int kPerThread = 500;  // 4*500 = 2000 < kCapacity，不会丢
    std::atomic<bool> stop{false};

    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&ds, t]() {
            for (int i = 0; i < kPerThread; ++i) {
                ds.push(mk("D" + std::to_string(t), i, "v", i));
            }
        });
    }

    std::thread consumer([&]() {
        int last = 0;
        while (!stop.load()) {
            auto [seq, _] = ds.snapshot_since(last);
            last = seq;
        }
    });

    for (auto& th : producers) th.join();
    stop.store(true);
    consumer.join();

    EXPECT_EQ(ds.max_seq(), static_cast<int64_t>(kThreads * kPerThread));
    EXPECT_EQ(ds.size(), static_cast<size_t>(kThreads * kPerThread));
    EXPECT_EQ(ds.dropped(), 0);

    auto [_, samples] = ds.snapshot_since(0);
    ASSERT_EQ(samples.size(), static_cast<size_t>(kThreads * kPerThread));
    std::set<int64_t> seen;
    int64_t prev = 0;
    for (const auto& m : samples) {
        int64_t sq = std::stoll(m.at("seq"));
        EXPECT_GT(sq, prev);
        prev = sq;
        EXPECT_TRUE(seen.insert(sq).second);  // 无重复 seq
    }
    EXPECT_EQ(seen.size(), static_cast<size_t>(kThreads * kPerThread));
}