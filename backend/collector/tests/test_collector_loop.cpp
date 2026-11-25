#include <gtest/gtest.h>

#include "common/event_emitter.h"
#include "drivers/driver.h"
#include "plan/plan.h"
#include "scheduler/collector_loop.h"
#include "store/data_store.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace ibmcp;

namespace {

std::shared_ptr<const Plan> make_plan(int revision = 1) {
    auto p = std::make_shared<Plan>();
    p->revision = revision;
    Device d;
    d.device_key = "DEV1";
    d.protocol = "fake";
    Point pt;
    pt.point_id = 101;
    pt.alarm_high = 100.0;
    pt.unit = "\xc2\xb0" "C";
    pt.cycle_ms = 100; // 让 350ms 窗口内能触发多次采集
    d.points.push_back(pt);
    p->devices.push_back(d);
    return p;
}

// 注入用 fake 驱动：connect 始终成功；fail_read=true 时 read_batch 返回 Bad 质量；
// fail_write=true 时 write 返回 false；set_throw_write(true) 时 write 抛异常（验证吞异常）。
class FakeDriver : public IDriver {
public:
    explicit FakeDriver(bool fail_read, bool fail_write = false)
        : fail_read_(fail_read), fail_write_(fail_write) {}
    std::string protocol() const override { return "fake"; }
    bool connect(const nlohmann::json&, int, int) override {
        connected_.store(true);
        return true;
    }
    std::vector<ReadResult> read_batch(const std::vector<Point>& points) override {
        ++calls_;
        std::vector<ReadResult> out;
        for (const auto& pt : points) {
            ReadResult r;
            r.point_id = pt.point_id;
            if (fail_read_) {
                r.quality = "Bad";
                r.error = "simulated failure";
            } else {
                r.value = "42.000";
                r.quality = "Good";
            }
            out.push_back(r);
        }
        return out;
    }
    bool write(const Point& pt, const std::string& value) override {
        ++write_calls_;
        last_write_point_.store(pt.point_id);
        {
            std::lock_guard<std::mutex> lk(val_mtx_);
            last_write_value_ = value;
        }
        if (throw_write_) throw std::runtime_error("simulated write exception");
        return !fail_write_;
    }
    void set_throw_write(bool v) { throw_write_ = v; }
    void disconnect() override { connected_.store(false); }
    int calls() const { return calls_.load(); }
    int write_calls() const { return write_calls_.load(); }
    int last_write_point() const { return last_write_point_.load(); }
    std::string last_write_value() const {
        std::lock_guard<std::mutex> lk(val_mtx_);
        return last_write_value_;
    }
    bool connected() const { return connected_.load(); }

private:
    bool fail_read_;
    bool fail_write_;
    bool throw_write_ = false;
    std::atomic<bool> connected_{false};
    std::atomic<int> calls_{0};
    std::atomic<int> write_calls_{0};
    std::atomic<int> last_write_point_{0};
    mutable std::mutex val_mtx_;
    std::string last_write_value_;
};

} // namespace

TEST(CollectorLoop, ProducesSamplesAfterStart) {
    DataStore store;
    EventEmitter emitter;
    auto plan = make_plan();
    CollectorLoop loop(store, emitter, [&] { return plan; });
    loop.set_interval_ms(100);
    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    loop.stop();

    EXPECT_GT(store.max_seq(), 0);
    EXPECT_GE(store.size(), 1u);
    auto snap = store.snapshot_since(0);
    ASSERT_GE(snap.second.size(), 1u);
    const auto& first = snap.second.front();
    EXPECT_EQ(first.at("device_key"), "DEV1");
    EXPECT_EQ(first.at("point_id"), "101");
    EXPECT_FALSE(first.at("value").empty());
}

TEST(CollectorLoop, NoPlanProducesNoSamples) {
    DataStore store;
    EventEmitter emitter;
    CollectorLoop loop(store, emitter,
                       [] { return std::shared_ptr<const Plan>(); });
    loop.set_interval_ms(50);
    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    loop.stop();
    EXPECT_EQ(store.max_seq(), 0);
    EXPECT_EQ(store.size(), 0u);
}

TEST(CollectorLoop, StopIsIdempotentAndFast) {
    DataStore store;
    EventEmitter emitter;
    auto plan = make_plan();
    CollectorLoop loop(store, emitter, [&] { return plan; });
    loop.set_interval_ms(5000);
    loop.start();
    auto t0 = std::chrono::steady_clock::now();
    loop.stop();
    loop.stop();
    auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();
    EXPECT_LT(dt, 500);
}

TEST(CollectorLoop, HotPlanSwapWithoutRestart) {
    DataStore store;
    EventEmitter emitter;
    auto plan_a = make_plan(1);
    auto current = std::make_shared<std::shared_ptr<const Plan>>(plan_a);
    CollectorLoop loop(store, emitter, [&] { return *current; });
    loop.set_interval_ms(80);
    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    EXPECT_GT(store.max_seq(), 0);

    auto plan_b = std::make_shared<Plan>();
    plan_b->revision = 2; // 与 plan_a 不同，触发重载
    Device d;
    d.device_key = "DEV2";
    d.protocol = "fake";
    Point pt;
    pt.point_id = 202;
    pt.cycle_ms = 100;
    d.points.push_back(pt);
    plan_b->devices.push_back(d);
    int64_t seq_before = store.max_seq();
    *current = plan_b;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    loop.stop();

    auto snap = store.snapshot_since(static_cast<int>(seq_before));
    bool saw_dev2 = false;
    for (const auto& s : snap.second) {
        if (s.at("device_key") == "DEV2") saw_dev2 = true;
    }
    EXPECT_TRUE(saw_dev2);
}

TEST(CollectorLoop, DriverFactoryInjectionWritesRealValues) {
    DataStore store;
    EventEmitter emitter;
    auto plan = make_plan(1);
    auto factory = [](const std::string&) {
        return std::unique_ptr<IDriver>(new FakeDriver(false));
    };
    CollectorLoop loop(store, emitter, [&] { return plan; }, factory, 2);
    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    loop.stop();

    EXPECT_GT(store.max_seq(), 0);
    auto snap = store.snapshot_since(0);
    ASSERT_FALSE(snap.second.empty());
    bool saw_good_value = false;
    for (const auto& s : snap.second) {
        if (s.at("value") == "42.000" && s.at("quality") == "Good") {
            saw_good_value = true;
        }
    }
    EXPECT_TRUE(saw_good_value);
}

TEST(CollectorLoop, DriverReadFailureWritesBadQuality) {
    DataStore store;
    EventEmitter emitter;
    auto plan = make_plan(1);
    auto factory = [](const std::string&) {
        return std::unique_ptr<IDriver>(new FakeDriver(true));
    };
    CollectorLoop loop(store, emitter, [&] { return plan; }, factory, 2);
    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    loop.stop();

    EXPECT_GT(store.max_seq(), 0);
    auto snap = store.snapshot_since(0);
    ASSERT_FALSE(snap.second.empty());
    bool saw_bad = false;
    for (const auto& s : snap.second) {
        if (s.at("quality") == "Bad") saw_bad = true;
    }
    EXPECT_TRUE(saw_bad);
}

namespace {

// 构造一个捕获 raw 指针的注入工厂，便于断言驱动 write 行为。
std::function<std::unique_ptr<IDriver>(const std::string&)> make_capturing_factory(
    FakeDriver*& out, bool fail_read, bool fail_write) {
    return [&out, fail_read, fail_write](const std::string&) {
        auto d = std::unique_ptr<FakeDriver>(new FakeDriver(fail_read, fail_write));
        out = d.get();
        return std::unique_ptr<IDriver>(d.release());
    };
}

} // namespace

TEST(CollectorLoop, WriteDeviceSucceeds) {
    DataStore store;
    EventEmitter emitter;
    auto plan = make_plan(1);
    FakeDriver* fake = nullptr;
    CollectorLoop loop(store, emitter, [&] { return plan; },
                       make_capturing_factory(fake, false, false), 2);
    loop.set_interval_ms(50);
    loop.start(); // start 内同步 check_reload，驱动已构建
    bool ok = loop.write_device("DEV1", 101, "99.5");
    loop.stop();

    EXPECT_TRUE(ok);
    ASSERT_NE(fake, nullptr);
    EXPECT_EQ(fake->write_calls(), 1);
    EXPECT_EQ(fake->last_write_point(), 101);
    EXPECT_EQ(fake->last_write_value(), "99.5");
}

TEST(CollectorLoop, WriteDeviceFailureReturnsFalse) {
    DataStore store;
    EventEmitter emitter;
    auto plan = make_plan(1);
    FakeDriver* fake = nullptr;
    CollectorLoop loop(store, emitter, [&] { return plan; },
                       make_capturing_factory(fake, false, true), 2);
    loop.set_interval_ms(50);
    loop.start();
    bool ok = loop.write_device("DEV1", 101, "1");
    loop.stop();

    EXPECT_FALSE(ok);
    ASSERT_NE(fake, nullptr);
    EXPECT_EQ(fake->write_calls(), 1);
}

TEST(CollectorLoop, WriteDeviceThrowsIsSwallowed) {
    DataStore store;
    EventEmitter emitter;
    auto plan = make_plan(1);
    FakeDriver* fake = nullptr;
    CollectorLoop loop(store, emitter, [&] { return plan; },
                       make_capturing_factory(fake, false, false), 2);
    loop.set_interval_ms(50);
    loop.start();
    fake->set_throw_write(true);
    bool ok = loop.write_device("DEV1", 101, "1");
    loop.stop();

    EXPECT_FALSE(ok);
    EXPECT_EQ(fake->write_calls(), 1); // write 进入后才抛，调用计数应已自增
}

TEST(CollectorLoop, WriteDeviceUnknownPointReturnsFalse) {
    DataStore store;
    EventEmitter emitter;
    auto plan = make_plan(1);
    FakeDriver* fake = nullptr;
    CollectorLoop loop(store, emitter, [&] { return plan; },
                       make_capturing_factory(fake, false, false), 2);
    loop.set_interval_ms(50);
    loop.start();
    bool ok = loop.write_device("DEV1", 999, "1"); // 点位 999 不在 plan 中
    loop.stop();

    EXPECT_FALSE(ok);
    EXPECT_EQ(fake->write_calls(), 0); // 点位未命中，不应触达驱动
}

TEST(CollectorLoop, WriteDeviceUnknownDeviceReturnsFalse) {
    DataStore store;
    EventEmitter emitter;
    auto plan = make_plan(1);
    FakeDriver* fake = nullptr;
    CollectorLoop loop(store, emitter, [&] { return plan; },
                       make_capturing_factory(fake, false, false), 2);
    loop.set_interval_ms(50);
    loop.start();
    bool ok = loop.write_device("NOPE", 101, "1"); // 设备不在 plan 中
    loop.stop();

    EXPECT_FALSE(ok);
    EXPECT_EQ(fake->write_calls(), 0);
}

namespace {

// 可变连接行为的 fake 驱动：connect 成败由外部共享原子控制，
// 便于测试「先掉线降级 → 放行后重连恢复」的时序；fail_read 控制读质量。
class FlakyDriver : public IDriver {
public:
    FlakyDriver(std::shared_ptr<std::atomic<bool>> connect_ok, bool fail_read)
        : connect_ok_(std::move(connect_ok)), fail_read_(fail_read) {}
    std::string protocol() const override { return "fake"; }
    bool connect(const nlohmann::json&, int, int) override {
        bool ok = connect_ok_->load();
        connected_.store(ok);
        return ok;
    }
    std::vector<ReadResult> read_batch(const std::vector<Point>& points) override {
        std::vector<ReadResult> out;
        for (const auto& pt : points) {
            ReadResult r;
            r.point_id = pt.point_id;
            if (fail_read_) {
                r.quality = "Bad";
                r.error = "simulated read failure";
            } else {
                r.value = "42.000";
                r.quality = "Good";
            }
            out.push_back(r);
        }
        return out;
    }
    bool write(const Point&, const std::string&) override { return true; }
    void disconnect() override { connected_.store(false); }

private:
    std::shared_ptr<std::atomic<bool>> connect_ok_;
    bool fail_read_;
    std::atomic<bool> connected_{false};
};

// 构造注入工厂：每次创建 FlakyDriver 时共享同一个 connect_ok 原子，
// 使「运行中切换连接成败」可被后续新建的驱动实例观测到（用于重连恢复测试）。
// 注意：lambda 按值捕获 shared_ptr，调用时传拷贝（不可 move 捕获变量，
// 否则首次调用后掏空，try_reconnect 下一次建驱动会解引用空指针）。
std::function<std::unique_ptr<IDriver>(const std::string&)> make_flaky_factory(
    std::shared_ptr<std::atomic<bool>> connect_ok, bool fail_read) {
    return [connect_ok, fail_read](const std::string&) {
        return std::unique_ptr<IDriver>(new FlakyDriver(connect_ok, fail_read));
    };
}

} // namespace

TEST(CollectorLoop, ConnectFailureDegradesAndEmitsStatus) {
    DataStore store;
    EventEmitter emitter;
    auto plan = make_plan(1);
    auto connect_ok = std::make_shared<std::atomic<bool>>(false);
    std::vector<EventEmitter::Event> events;
    std::mutex ev_mtx;
    emitter.set_callback([&](const EventEmitter::Event& e) {
        std::lock_guard<std::mutex> lk(ev_mtx);
        events.push_back(e);
    });
    CollectorLoop loop(store, emitter, [&] { return plan; },
                       make_flaky_factory(connect_ok, false), 1);
    loop.set_reconnect_interval_ms(0); // 关闭重连，专注断言初始降级
    loop.start();                      // start 内同步 reload
    auto counts = loop.device_counts();
    loop.stop();

    EXPECT_EQ(counts.first, 1);
    EXPECT_EQ(counts.second, 1); // connect 失败 → 降级
    bool saw = false;
    {
        std::lock_guard<std::mutex> lk(ev_mtx);
        for (const auto& e : events) {
            if (e.at("type") == "device_status" && e.at("device_key") == "DEV1" &&
                e.at("status") == "0" && e.at("reason") == "connect_failed" &&
                e.at("source") == "sim") {
                saw = true;
            }
        }
    }
    EXPECT_TRUE(saw);
}

TEST(CollectorLoop, ReadFailureMarksDeviceDegraded) {
    DataStore store;
    EventEmitter emitter;
    auto plan = make_plan(1); // 点位 cycle_ms=100
    auto connect_ok = std::make_shared<std::atomic<bool>>(true);
    std::vector<EventEmitter::Event> events;
    std::mutex ev_mtx;
    emitter.set_callback([&](const EventEmitter::Event& e) {
        std::lock_guard<std::mutex> lk(ev_mtx);
        events.push_back(e);
    });
    CollectorLoop loop(store, emitter, [&] { return plan; },
                       make_flaky_factory(connect_ok, true), 1); // connect 成功但读全 Bad
    loop.set_reconnect_interval_ms(0);
    loop.set_interval_ms(50);
    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(350)); // 触发多次采集
    auto counts = loop.device_counts();
    loop.stop();

    EXPECT_EQ(counts.first, 1);
    EXPECT_EQ(counts.second, 1); // 整设备无 Good 点位 → 降级
    bool saw = false;
    {
        std::lock_guard<std::mutex> lk(ev_mtx);
        for (const auto& e : events) {
            if (e.at("type") == "device_status" && e.at("reason") == "read_failed" &&
                e.at("status") == "0") {
                saw = true;
            }
        }
    }
    EXPECT_TRUE(saw);
}

TEST(CollectorLoop, HealthyDeviceCountsAsReal) {
    DataStore store;
    EventEmitter emitter;
    auto plan = make_plan(1);
    auto connect_ok = std::make_shared<std::atomic<bool>>(true);
    CollectorLoop loop(store, emitter, [&] { return plan; },
                       make_flaky_factory(connect_ok, false), 1);
    loop.set_reconnect_interval_ms(0);
    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    auto counts = loop.device_counts();
    loop.stop();

    EXPECT_EQ(counts.first, 1);
    EXPECT_EQ(counts.second, 0); // 连接正常 + 读 Good → 不降级
}

TEST(CollectorLoop, ReconnectRestoresDegradedDevice) {
    DataStore store;
    EventEmitter emitter;
    auto plan = make_plan(1);
    auto connect_ok = std::make_shared<std::atomic<bool>>(false); // 初始掉线
    std::vector<EventEmitter::Event> events;
    std::mutex ev_mtx;
    emitter.set_callback([&](const EventEmitter::Event& e) {
        std::lock_guard<std::mutex> lk(ev_mtx);
        events.push_back(e);
    });
    CollectorLoop loop(store, emitter, [&] { return plan; },
                       make_flaky_factory(connect_ok, false), 1);
    loop.set_reconnect_interval_ms(80); // 缩短重连周期，加速测试
    loop.start();
    EXPECT_EQ(loop.device_counts().second, 1); // 初始降级

    connect_ok->store(true); // 放行连接
    std::this_thread::sleep_for(std::chrono::milliseconds(400)); // 覆盖若干重连周期
    auto counts = loop.device_counts();
    loop.stop();

    EXPECT_EQ(counts.first, 1);
    EXPECT_EQ(counts.second, 0); // 重连成功 → 恢复真实采集
    bool saw = false;
    {
        std::lock_guard<std::mutex> lk(ev_mtx);
        for (const auto& e : events) {
            if (e.at("type") == "device_status" && e.at("reason") == "reconnected" &&
                e.at("status") == "1" && e.at("source") == "real") {
                saw = true;
            }
        }
    }
    EXPECT_TRUE(saw);
}

TEST(CollectorLoop, ReconnectDisabledKeepsDegraded) {
    DataStore store;
    EventEmitter emitter;
    auto plan = make_plan(1);
    auto connect_ok = std::make_shared<std::atomic<bool>>(false);
    CollectorLoop loop(store, emitter, [&] { return plan; },
                       make_flaky_factory(connect_ok, false), 1);
    loop.set_reconnect_interval_ms(0); // 关闭重连
    loop.start();
    EXPECT_EQ(loop.device_counts().second, 1);

    connect_ok->store(true); // 即使放行也不会触发重连
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    auto counts = loop.device_counts();
    loop.stop();

    EXPECT_EQ(counts.first, 1);
    EXPECT_EQ(counts.second, 1); // 重连已关闭，维持降级
}