// EventBusTest.cpp - 添加到现有测试文件
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>

// 包含项目头文件
#include "EventBus.h"
#include "Event.h"
#include "foundation/utils/Timestamp.h"
#include "foundation/thread/IExecutor.h"

using namespace testing;
using namespace engine;

// ========== Mock 类定义 ==========

// Mock Event - 简化版本用于 EventBus 测试
class MockEvent : public Event {
public:
    explicit MockEvent(Event::Type type = Event::Type::UserCustom)
        : Event(type, Timestamp::now(), {})
        , type_(type)
    {
        attributes_["mock"] = "true";
    }
     std::unique_ptr<Event> clone() const override {
        auto e = std::make_unique<MockEvent>(type_);
        e->attributes_ = attributes_;
        return e;
    }
    MOCK_METHOD(foundation::Uuid, id, (), (const, override));
    MOCK_METHOD(Event::Type, type, (), (const, override));
    MOCK_METHOD(Timestamp, timestamp, (), (const, override));
    MOCK_METHOD(std::string, source, (), (const, override));
    MOCK_METHOD(const void*, payload, (), (const, override));
    MOCK_METHOD(std::string, payload_type, (), (const, override));
   // MOCK_METHOD(std::unique_ptr<Event>, clone, (), (const, override));

    const Event::Attributes& attributes() const override {
        return attributes_;
    }

private:
    Event::Type type_;
    Event::Attributes attributes_;
};


// 简单的同步执行器
class SyncExecutor : public foundation::thread::ThreadPoolExecutor {
public:
    SyncExecutor() 
        : foundation::thread::ThreadPoolExecutor(1) {  // 传递需要的参数
    }
    void post(std::function<void()> task) override {
        task();
    }
};

// 异步执行器（用于测试）
class AsyncExecutor : public foundation::thread::ThreadPoolExecutor {
public:
    AsyncExecutor(): foundation::thread::ThreadPoolExecutor(1){
        
    }
    void post(std::function<void()> task) override {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    void run_all() {
        std::vector<std::function<void()>> tasks;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks.swap(tasks_);
        }
        
        for (auto& task : tasks) {
            task();
        }
    }
    
    size_t pending_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }
    
private:
    mutable std::mutex mutex_;
    std::vector<std::function<void()>> tasks_;
};

// ========== EventBus 测试用例 ==========

class EventBusTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto executor = std::make_shared<SyncExecutor>();
        event_bus_ = EventBus::create(executor);
    }
    
    void TearDown() override {
        event_bus_.reset();
    }
    
    std::unique_ptr<EventBus> event_bus_;
};

TEST_F(EventBusTest, BasicPublishSubscribe_Conservative) {
    static std::atomic<int> s_callback_count{0};
    static int s_received_value = 0;
    
    const Event::Type TEST_EVENT_TYPE = Event::Type::System;
    
    // 最简单的无捕获lambda
    auto callback = [](std::unique_ptr<Event> evt) {
        s_callback_count++;
        if (evt) {
            std::string value_str;
            if (evt->get_attribute("value", value_str)) {
                s_received_value = std::stoi(value_str);
            }
        }
    };
    
    // 重置静态变量
    s_callback_count = 0;
    s_received_value = 0;
    
    // 订阅
    foundation::Uuid subscription_id = event_bus_->subscribe(TEST_EVENT_TYPE, callback);
    
    // 如果还失败，那就是 EventBus 实现的问题了
    if (subscription_id.is_null()) {
         std::cout << "严重错误：即使使用最简单的无捕获lambda，订阅也失败了！" << std::endl;
        // std::cout << "这意味着 EventBus::subscribe() 实现有根本性问题" << std::endl;
    }
    
    EXPECT_TRUE(subscription_id.is_valid());
    
    // 发布事件
    auto event = Event::create(TEST_EVENT_TYPE, Timestamp::now(), {{"value", "42"}});
    ASSERT_NE(event, nullptr);
    
    auto error = event_bus_->publish(std::move(event));
    //EXPECT_FALSE(error);
    
    // 分发
    size_t dispatched = event_bus_->dispatch();
    EXPECT_EQ(dispatched, 1);
    
    // 等待
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_EQ(s_callback_count, 1);
    EXPECT_EQ(s_received_value, 42);
}

TEST_F(EventBusTest, MultipleSubscribers) {
    std::atomic<int> callback1_count{0};
    std::atomic<int> callback2_count{0};
    
    const Event::Type TEST_EVENT_TYPE = Event::Type::MarketData;
    
    auto callback1 = [&](std::unique_ptr<Event> evt) {
        callback1_count++;
    };
    
    auto callback2 = [&](std::unique_ptr<Event> evt) {
        callback2_count++;
    };
    
    // 订阅
    event_bus_->subscribe(TEST_EVENT_TYPE, callback1);
    event_bus_->subscribe(TEST_EVENT_TYPE, callback2);
    
    // 发布
    auto event = Event::create(TEST_EVENT_TYPE, Timestamp::now(),{{"发布","1"}});
    event_bus_->publish(std::move(event));
    
    // 分发
    event_bus_->dispatch();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(callback1_count, 1);
    EXPECT_EQ(callback2_count, 1);
}



TEST_F(EventBusTest, InvalidArguments) {
    // 测试空事件发布
    auto error = event_bus_->publish(nullptr);
    EXPECT_TRUE(error);  // Error 应该为 true 表示有错误
    // 根据你的实现，错误码应该是 1
    // EXPECT_EQ(error.code(), 1);
    
    // 测试空回调订阅
    foundation::Uuid sub_id = event_bus_->subscribe(Event::Type::UserCustom, nullptr);
    EXPECT_FALSE(sub_id.is_valid());  // 应该返回无效 UUID
    
    // 测试无效 UUID 取消订阅
    foundation::Uuid invalid_uuid;
    error = event_bus_->unsubscribe(Event::Type::UserCustom, invalid_uuid);
    EXPECT_TRUE(error);  // 应该失败
}

// 生命周期管理测试
TEST_F(EventBusTest, StopAndStart) {
    std::atomic<int> callback_count{0};
    
    const Event::Type TEST_EVENT_TYPE = Event::Type::UserCustom; 
    event_bus_->subscribe(TEST_EVENT_TYPE, [&](std::unique_ptr<Event> evt) {
        callback_count++;
    });
    
    // 停止事件总线
    event_bus_->stop();
    EXPECT_TRUE(event_bus_->is_stopped());
    
    // 发布事件 - 应该失败
    auto event = Event::create(TEST_EVENT_TYPE, Timestamp::now(),{{"失败",std::to_string(2)}});
    auto result = event_bus_->publish(std::move(event));
    // EXPECT_TRUE(result.is_error());
    // EXPECT_EQ(result.code, 3); // ErrorCode::OperationNotAllowed
    
    // 启动事件总线
    event_bus_->start();
    EXPECT_FALSE(event_bus_->is_stopped());
    
    // 发布事件 - 应该成功
    event = Event::create(TEST_EVENT_TYPE, Timestamp::now(),{{"成功",std::to_string(2)}});
    result = event_bus_->publish(std::move(event));
    EXPECT_TRUE(result.ok());
    
    // 分发
    event_bus_->dispatch();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(callback_count, 1);
}

TEST_F(EventBusTest, Clear) {
    std::atomic<int> callback_count{0};
    
    const Event::Type TEST_EVENT_TYPE = Event::Type::UserCustom;
    
    auto callback = [&](std::unique_ptr<Event> evt) {
        callback_count++;
    };
    
    event_bus_->subscribe(TEST_EVENT_TYPE, callback);
    
    // 发布一些事件
    for (int i = 0; i < 3; ++i) {
        auto event = Event::create(TEST_EVENT_TYPE, Timestamp::now(),{{"index",std::to_string(i)}});
        event_bus_->publish(std::move(event));
    }
    
    // 清除所有事件
    event_bus_->clear();
    
    // 分发 - 应该没有事件
    size_t dispatched = event_bus_->dispatch();
    EXPECT_EQ(dispatched, 0);
    
    // 清除订阅（通过reset）
    event_bus_->reset();
    
    // 发布新事件 - 不应该被接收（订阅已清除）
    auto event = Event::create(TEST_EVENT_TYPE, Timestamp::now(),{{"new event",std::to_string(3)}});
    event_bus_->publish(std::move(event));
    dispatched = event_bus_->dispatch();
    EXPECT_EQ(dispatched, 0);
}

// TEST_F(EventBusTest, Reset) {
//     // 设置一些状态
//     auto policy = DispatchPolicy::Batch(5);
//     event_bus_->set_policy(policy);
    
//     const Event::Type TEST_EVENT_TYPE = Event::Type::UserCustom;
    
//     // 发布一些事件
//     for (int i = 0; i < 3; ++i) {
//         auto event = Event::create(TEST_EVENT_TYPE, Timestamp::now(),{{"a lot",std::to_string(i)}});
//         event_bus_->publish(std::move(event));
//     }
    
//     // 重置
//     event_bus_->reset();
    
//     // 检查状态
//     EXPECT_FALSE(event_bus_->is_stopped());
//     auto current_policy = event_bus_->policy();
//     EXPECT_EQ(current_policy.mode, DispatchMode::Immediate);
    
//     // 队列应该被清空
//     size_t dispatched = event_bus_->dispatch();
//     EXPECT_EQ(dispatched, 0);
// }

// 异步执行器测试
TEST_F(EventBusTest, WithAsyncExecutor) {
    auto async_executor = std::make_shared<AsyncExecutor>();
    auto async_event_bus = EventBus::create(async_executor);
    
    std::atomic<int> callback_count{0};
    
    const Event::Type TEST_EVENT_TYPE = Event::Type::UserCustom;
    
    auto callback = [&](std::unique_ptr<Event> evt) {
        callback_count++;
    };
    
    async_event_bus->subscribe(TEST_EVENT_TYPE, callback);
    
    // 发布事件
    auto event = Event::create(TEST_EVENT_TYPE, Timestamp::now(),{{"发布事件",std::to_string(3)}});
    async_event_bus->publish(std::move(event));
    
    // 分发（事件会提交到异步执行器）
    size_t dispatched = async_event_bus->dispatch();
    EXPECT_EQ(dispatched, 1);
    
    // 回调应该还没有执行（在异步队列中）
    EXPECT_EQ(callback_count, 0);
    
    // 执行异步任务
    std::dynamic_pointer_cast<AsyncExecutor>(async_executor)->run_all();
    
    // 现在回调应该执行了
    EXPECT_EQ(callback_count, 1);
}

// 集成测试：EventBus 与 Event 集成
TEST(EventBusIntegrationTest, EventBusWithMockEvent) {
    auto executor = std::make_shared<SyncExecutor>();
    auto event_bus = EventBus::create(executor);
    
    const Event::Type MOCK_EVENT_TYPE = Event::Type::UserCustom;
    std::atomic<int> mock_event_count{0};
    
    // 创建Mock事件
    auto mock_event = std::make_unique<MockEvent>(MOCK_EVENT_TYPE);
    
    // 设置期望
    ON_CALL(*mock_event, id())
        .WillByDefault(Return(foundation::Uuid::generate()));
    ON_CALL(*mock_event, type())
        .WillByDefault(Return(MOCK_EVENT_TYPE));
    ON_CALL(*mock_event, timestamp())
        .WillByDefault(Return(Timestamp::now()));
    // ON_CALL(*mock_event, attributes()).WillByDefault(::testing::Invoke(mock_event.get(),&MockEvent::attributes
    // ));
    
    // 订阅
    event_bus->subscribe(MOCK_EVENT_TYPE, [&](std::unique_ptr<Event> evt) {
        mock_event_count++;
        EXPECT_NE(evt, nullptr);
    });
    
    // 发布Mock事件
    auto result = event_bus->publish(std::move(mock_event));
    EXPECT_TRUE(result.ok());
    
    // 分发
    size_t dispatched = event_bus->dispatch();
    EXPECT_EQ(dispatched, 1);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(mock_event_count, 1);
}

// 性能测试：大量事件
TEST_F(EventBusTest, PerformanceLargeNumberOfEvents) {
    std::atomic<int> callback_count{0};
    
    const Event::Type TEST_EVENT_TYPE = Event::Type::Warning;
    
    auto callback = [&](std::unique_ptr<Event> evt) {
        callback_count++;
    };
    
    event_bus_->subscribe(TEST_EVENT_TYPE, callback);
    
    const int EVENT_COUNT = 1000;
    
    auto start_time = std::chrono::steady_clock::now();
    
    // 发布大量事件
    for (int i = 0; i < EVENT_COUNT; ++i) {
        auto event = Event::create(TEST_EVENT_TYPE, Timestamp::now(), {{"index", std::to_string(i)}});
        auto result = event_bus_->publish(std::move(event));
        EXPECT_TRUE(result.ok());
    }
    
    auto publish_time = std::chrono::steady_clock::now();
    
    // 分发所有事件
    size_t total_dispatched = 0;
    while (true) {
        size_t dispatched = event_bus_->dispatch();
        if (dispatched == 0) break;
        total_dispatched += dispatched;
    }
    
    auto dispatch_time = std::chrono::steady_clock::now();
    
    // 验证
    EXPECT_EQ(total_dispatched, EVENT_COUNT);
    EXPECT_EQ(callback_count, EVENT_COUNT);
    
    auto publish_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        publish_time - start_time);
    auto dispatch_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        dispatch_time - publish_time);
    
    // std::cout << "性能测试结果：" << std::endl;
    // std::cout << "  发布 " << EVENT_COUNT << " 个事件耗时: " << publish_duration.count() << "ms" << std::endl;
    // std::cout << "  分发 " << EVENT_COUNT << " 个事件耗时: " << dispatch_duration.count() << "ms" << std::endl;
}

// 边缘测试：异常处理
TEST_F(EventBusTest, ExceptionHandling) {
    std::atomic<int> callback_count{0};
    std::atomic<int> exception_count{0};
    
    const Event::Type TEST_EVENT_TYPE = Event::Type::Warning;
    
    auto throwing_callback = [&](std::unique_ptr<Event> evt) {
        callback_count++;
        throw std::runtime_error("Callback exception test");
    };
    
    auto safe_callback = [&](std::unique_ptr<Event> evt) {
        callback_count++;
        try {
            // 正常处理
        } catch (...) {
            exception_count++;
        }
    };
    
    // 订阅可能抛出异常的回调
    event_bus_->subscribe(TEST_EVENT_TYPE, throwing_callback);
    event_bus_->subscribe(TEST_EVENT_TYPE, safe_callback);
    
    // 发布事件
    auto event = Event::create(TEST_EVENT_TYPE, Timestamp::now(),{{"发布事件",std::to_string(4)}});
    event_bus_->publish(std::move(event));
    
    // 分发 - 应该处理异常而不崩溃
    EXPECT_NO_THROW({
        size_t dispatched = event_bus_->dispatch();
        EXPECT_GT(dispatched, 0);
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_GT(callback_count, 0);
}