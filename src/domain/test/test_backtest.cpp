// test_backtest.cpp
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <iostream>

#include "Bar.h"
#include "Position.h"
#include "MovingAverageStrategy.h"
#include "CrossSignal.h"
#include "StrategyEvent.h"
#include "EventBus.h"
#include "EventBusImpl.h"
#include "DispatchPolicy.h"

using namespace domain;
using namespace domain::model;
class TestStrategyEventSubscriber : public engine::EventSubscriber {
public:
    explicit TestStrategyEventSubscriber(std::vector<std::string>& out)
        : out_(out) {}

    void on_event(const engine::Event& event) override  {
        auto* se = dynamic_cast<const domain::StrategyEvent*>(&event);
        if (se) {
            out_.push_back(se->message());
        }
    }

private:
    std::vector<std::string>& out_;
};
TEST(BacktestEngineTest, MovingAverageStrategyBasic) {
    std::vector<Bar> bars = {
        {"APPL",16700000,100,105,95,102,1000},
        {"APPL",16700000,102,107,101,105,1200},
        {"APPL",16700000,105,110,104,108,1300},
        {"APPL",16700000,108,112,107,110,1100}
    };

    CrossSignal signal(2, 3);
    MovingAverageStrategy strategy(signal);

    auto executor = std::make_shared<foundation::thread::ThreadPoolExecutor>(4);
    auto bus = engine::EventBus::create(executor);
 // 强制同步

    strategy.setEventBus(bus.get());

    std::vector<std::string> event_messages;

    // Lambda 订阅事件
    bus->subscribe(engine::Event::Type::UserCustom, [&](std::unique_ptr<engine::Event> evt){
        std::string msg;
        evt->get_attribute("msg", msg);
        event_messages.push_back(msg);
    });

    Position position;

    for (auto& bar : bars) {
        auto action = strategy.onBar(bar);

        if (action == StrategyAction::OpenLong) {
            position.open(bar.close, 1);
            bus->publish(engine::Event::create(engine::Event::Type::UserCustom,
                                               foundation::Timestamp::now(),
                                               {{"msg","OpenLong"}}));
        } else if (action == StrategyAction::CloseLong) {
            position.close();
            bus->publish(engine::Event::create(engine::Event::Type::UserCustom,
                                               foundation::Timestamp::now(),
                                               {{"msg","CloseLong"}}));
        }
    }

    // 直接断言结果
    EXPECT_FALSE(position.hasPosition);
    EXPECT_EQ(position.entryPrice, 0.0);

    ASSERT_FALSE(event_messages.empty());
    for (auto& msg : event_messages) {
        std::cout << "Event: " << msg << std::endl;
    }
}

TEST(EventBusAdvancedTest, AsyncWithPolicyAndLambda) {
    // 使用 ThreadPoolExecutor 替代 SimpleExecutor
    auto executor = std::make_shared<foundation::thread::ThreadPoolExecutor>(2); // 2个线程
    auto bus = engine::EventBus::create(executor);

    // 设置策略（使用正确的语法）
    bus->set_policy(engine::DispatchPolicy::Immediate(10));

    std::mutex mtx;
    std::vector<std::string> received;
    std::condition_variable cv;
    std::atomic<int> completed_count{0};
    const int expected_count = 5;

    // 订阅事件
    bus->subscribe(engine::Event::Type::UserCustom, 
        [&](std::unique_ptr<engine::Event> evt) {
            if (!evt) return;
            
        std::string msg;
        evt->get_attribute("msg", msg);
            
            {
                std::lock_guard<std::mutex> lock(mtx);
        received.push_back(msg);
            }
            
            completed_count++;
            
            // 通知主线程
            if (completed_count >= expected_count) {
                cv.notify_one();
            }
        }
    );

    // 发布5个事件
    for (int i = 0; i < expected_count; ++i) {
        bus->publish(engine::Event::create(
            engine::Event::Type::UserCustom,
                                            foundation::Timestamp::now(),
            {{"msg", "evt" + std::to_string(i)}}
        ));
    }

    // 等待所有事件处理完成
    {
        std::unique_lock<std::mutex> lock(mtx);
        bool success = cv.wait_for(lock, std::chrono::seconds(2), 
                                  [&] { return completed_count >= expected_count; });
        
        EXPECT_TRUE(success) << "Timeout waiting for events to be processed";
    }

    // 验证结果
    {
    std::lock_guard<std::mutex> lock(mtx);
        EXPECT_EQ(received.size(), expected_count);
        
        // 验证内容（注意：由于是异步执行，顺序可能不同）
        std::vector<std::string> expected;
        for (int i = 0; i < expected_count; ++i) {
            expected.push_back("evt" + std::to_string(i));
        }
        
        // 排序后比较，因为异步执行顺序不确定
        std::sort(received.begin(), received.end());
        std::sort(expected.begin(), expected.end());
        
        EXPECT_EQ(received, expected);
    }

    // 清理：关闭执行器
    executor->shutdown(true);
}
