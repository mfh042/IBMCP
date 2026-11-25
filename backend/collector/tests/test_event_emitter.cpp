#include <gtest/gtest.h>

#include "common/event_emitter.h"

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace ibmcp;
using Event = EventEmitter::Event;

TEST(EventEmitter, NoCallbackDoesNotCrash) {
    EventEmitter e;
    EXPECT_NO_THROW(e.emit({{"type", "x"}}));
}

TEST(EventEmitter, CallbackInvokedWithEvent) {
    EventEmitter e;
    Event captured;
    e.set_callback([&](const Event& ev) { captured = ev; });
    e.emit({{"type", "plan_ack"}, {"revision", "5"}});
    ASSERT_EQ(captured.at("type"), "plan_ack");
    ASSERT_EQ(captured.at("revision"), "5");
}

TEST(EventEmitter, ClearStopsDelivery) {
    EventEmitter e;
    int count = 0;
    e.set_callback([&](const Event&) { ++count; });
    e.emit({{"type", "a"}});
    e.clear_callback();
    e.emit({{"type", "b"}});
    EXPECT_EQ(count, 1);
}

TEST(EventEmitter, ConsumerExceptionIsSwallowed) {
    EventEmitter e;
    bool delivered_after = false;
    e.set_callback([&](const Event& ev) {
        if (ev.at("type") == "bad") throw std::runtime_error("boom");
        delivered_after = true;
    });
    EXPECT_NO_THROW(e.emit({{"type", "bad"}}));
    EXPECT_NO_THROW(e.emit({{"type", "good"}}));
    EXPECT_TRUE(delivered_after);
}

TEST(EventEmitter, ConcurrentEmitIsSafe) {
    EventEmitter e;
    std::atomic<int> delivered{0};
    e.set_callback([&](const Event& ev) { (void)ev; ++delivered; });
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < 100; ++j) e.emit({{"type", "t"}});
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_EQ(delivered.load(), 400);
}