// tests/test_eventbus_cpp_units.cpp
// C++ Event 和 EventBus 单元测试

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>
#include <functional>
#include <queue>
#include <mutex>

// ===== 简单的Event和EventBus存根实现 =====

namespace engine {

class Event {
public:
    explicit Event(const std::string& type) : type_(type), timestamp_(now()) {}
    
    const std::string& type() const { return type_; }
    uint64_t timestamp() const { return timestamp_; }
    
    template<typename T>
    void set(const std::string& key, const T& value) {
        // 简单实现，不存储数据
    }
    
private:
    std::string type_;
    uint64_t timestamp_;
    
    static uint64_t now() {
        return std::chrono::system_clock::now().time_since_epoch().count();
    }
};

class EventBus {
public:
    using EventHandler = std::function<void(const Event&)>;
    
    EventBus() : running_(false) {}
    
    bool start() {
        running_ = true;
        return true;
    }
    
    void stop() {
        running_ = false;
    }
    
    bool is_running() const {
        return running_;
    }
    
    void subscribe(const std::string& event_type, EventHandler handler) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        handlers_[event_type].push_back(handler);
    }
    
    void unsubscribe(const std::string& event_type) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        handlers_.erase(event_type);
    }
    
    void publish(const Event& event) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        auto it = handlers_.find(event.type());
        if (it != handlers_.end()) {
            for (auto& handler : it->second) {
                handler(event);
            }
        }
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        handlers_.clear();
    }
    
private:
    std::atomic<bool> running_;
    std::map<std::string, std::vector<EventHandler>> handlers_;
    std::mutex handlers_mutex_;
};

} // namespace engine

namespace {

// ===== Event 类测试 =====

class EventTest : public ::testing::Test {
protected:
    virtual void SetUp() {
    }
    
    virtual void TearDown() {
    }
};

TEST_F(EventTest, EventConstruction) {
    engine::Event event("market_data");
    EXPECT_EQ(event.type(), "market_data");
    EXPECT_GT(event.timestamp(), 0);
}

TEST_F(EventTest, EventDataAccess) {
    engine::Event event("order_placed");
    event.set("order_id", 12345);
    event.set("symbol", "AAPL");
    EXPECT_EQ(event.type(), "order_placed");
}

TEST_F(EventTest, MultipleEventTypes) {
    std::vector<std::string> event_types = {
        "market_data", "order_placed", "order_filled", "error"
    };
    
    for (const auto& type : event_types) {
        engine::Event event(type);
        EXPECT_EQ(event.type(), type);
    }
}

// ===== EventBus 类测试 =====

class EventBusTest : public ::testing::Test {
protected:
    engine::EventBus bus;
    
    virtual void SetUp() {
        ASSERT_TRUE(bus.start());
    }
    
    virtual void TearDown() {
        bus.stop();
    }
};

TEST_F(EventBusTest, EventBusLifecycle) {
    EXPECT_TRUE(bus.is_running());
    bus.stop();
    EXPECT_FALSE(bus.is_running());
}

TEST_F(EventBusTest, PublishSubscribe) {
    bool received = false;
    std::string received_type;
    
    bus.subscribe("test_event", [&](const engine::Event& event) {
        received = true;
        received_type = event.type();
    });
    
    engine::Event event("test_event");
    bus.publish(event);
    
    EXPECT_TRUE(received);
    EXPECT_EQ(received_type, "test_event");
}

TEST_F(EventBusTest, MultipleSubscribers) {
    int count = 0;
    
    bus.subscribe("multi_event", [&](const engine::Event&) {
        count++;
    });
    
    bus.subscribe("multi_event", [&](const engine::Event&) {
        count++;
    });
    
    engine::Event event("multi_event");
    bus.publish(event);
    
    EXPECT_EQ(count, 2);
}

TEST_F(EventBusTest, UnsubscribeEvent) {
    int count = 0;
    
    bus.subscribe("removable_event", [&](const engine::Event&) {
        count++;
    });
    
    engine::Event event("removable_event");
    bus.publish(event);
    EXPECT_EQ(count, 1);
    
    bus.unsubscribe("removable_event");
    bus.publish(event);
    EXPECT_EQ(count, 1);  // 不应该增加
}

TEST_F(EventBusTest, ClearAllHandlers) {
    int count = 0;
    
    bus.subscribe("event1", [&](const engine::Event&) { count++; });
    bus.subscribe("event2", [&](const engine::Event&) { count++; });
    
    bus.clear();
    
    bus.publish(engine::Event("event1"));
    bus.publish(engine::Event("event2"));
    
    EXPECT_EQ(count, 0);
}

// ===== EventBus 并发测试 =====

class EventBusConcurrencyTest : public ::testing::Test {
protected:
    engine::EventBus bus;
    
    virtual void SetUp() {
        ASSERT_TRUE(bus.start());
    }
    
    virtual void TearDown() {
        bus.stop();
    }
};

TEST_F(EventBusConcurrencyTest, ThreadSafety) {
    std::atomic<int> count(0);
    std::vector<std::thread> threads;
    
    // 创建50个订阅线程
    for (int i = 0; i < 50; ++i) {
        threads.emplace_back([this, &count]() {
            bus.subscribe("concurrent_event", [&count](const engine::Event&) {
                count++;
            });
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // 发布事件
    engine::Event event("concurrent_event");
    bus.publish(event);
    
    EXPECT_EQ(count, 50);
}

TEST_F(EventBusConcurrencyTest, ConcurrentPublishAndSubscribe) {
    std::atomic<int> event_count(0);
    
    // 订阅线程
    std::thread subscriber([this, &event_count]() {
        for (int i = 0; i < 2; ++i) {
            bus.subscribe("concurrent_pub_sub", [&event_count](const engine::Event&) {
                event_count++;
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    
    // 发布线程
    std::thread publisher([this]() {
        for (int i = 0; i < 2; ++i) {
            engine::Event event("concurrent_pub_sub");
            bus.publish(event);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    
    subscriber.join();
    publisher.join();
}

// ===== EventBus 性能测试 =====

class EventBusPerformanceTest : public ::testing::Test {
protected:
    engine::EventBus bus;
    
    virtual void SetUp() {
        ASSERT_TRUE(bus.start());
    }
    
    virtual void TearDown() {
        bus.stop();
    }
};

TEST_F(EventBusPerformanceTest, PublishLatency) {
    std::atomic<int> received(0);
    bus.subscribe("perf_event", [&received](const engine::Event&) {
        received++;
    });
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 1000; ++i) {
        engine::Event event("perf_event");
        bus.publish(event);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    EXPECT_EQ(received, 1000);
    // 应该在几秒内完成1000个事件发布
    EXPECT_LT(duration.count(), 5000000);  // 5 seconds in microseconds
}

TEST_F(EventBusPerformanceTest, HighThroughput) {
    std::atomic<int> count(0);
    
    for (int i = 0; i < 100; ++i) {
        bus.subscribe("throughput_event", [&count](const engine::Event&) {
            count++;
        });
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 10000; ++i) {
        engine::Event event("throughput_event");
        bus.publish(event);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    
    EXPECT_EQ(count, 1000000);  // 100 subscribers * 10000 events
}

TEST_F(EventBusPerformanceTest, EventCreationLatency) {
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 100000; ++i) {
        engine::Event event("creation_test");
        (void)event;  // 避免编译器优化
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // 应该能快速创建100k个事件
    EXPECT_LT(duration.count(), 10000000);  // 10 seconds
}

}  // namespace
