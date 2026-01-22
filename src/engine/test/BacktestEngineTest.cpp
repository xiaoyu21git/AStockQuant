// test_engine_gtest_fixed.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <chrono>
#include <ctime>
#include <time.h>
#include <iostream>
#include <iomanip>
#include <sstream>

// 包含项目头文件
#include "Event.h"
#include "IDataSource.h"
#include "ITrigger.h"
#include "IClock.h"
#include "BacktestResult.h"
#include "foundation.h"

using namespace testing;
using namespace engine;

// ========== Mock 类定义 ==========

// Mock Event - 正确的实现
// class MockEvent : public Event {
// public:
//     MockEvent() : Event(Event::Type::UserCustom, Timestamp::now(), {}) {
//         // 设置默认属性
//         default_attrs_["source"] = "mock";
//         default_attrs_["created_by"] = "MockEvent";
        
//         // 为 attributes() 设置默认返回值
//         ON_CALL(*this, attributes())
//             .WillByDefault(ReturnRef(default_attrs_));
            
//         // 为其他纯虚函数设置默认值
//         ON_CALL(*this, id())
//             .WillByDefault(Return(foundation::Uuid::generate()));
//         ON_CALL(*this, type())
//             .WillByDefault(Return(Event::Type::UserCustom));
//         ON_CALL(*this, timestamp())
//             .WillByDefault(Return(Timestamp::now()));
//         ON_CALL(*this, source())
//             .WillByDefault(Return("mock_source"));
//     }
    
//     // 需要 Mock 所有纯虚函数
//     MOCK_METHOD(foundation::Uuid, id, (), (const, override));
//     MOCK_METHOD(Event::Type, type, (), (const, override));
//     MOCK_METHOD(Timestamp, timestamp, (), (const, override));
//     MOCK_METHOD(std::string, source, (), (const, override));
//     MOCK_METHOD(const void*, payload, (), (const, override));
//     MOCK_METHOD(std::string, payload_type, (), (const, override));
//     MOCK_METHOD(const Event::Attributes&, attributes, (), (const, override));
//     MOCK_METHOD(std::unique_ptr<Event>, clone, (), (const, override));
    
//     // 注意：不需要实现 has_attribute 和 get_attribute！
//     // 它们是非虚函数，直接继承基类的实现
    
// private:
//     Event::Attributes default_attrs_;
// };
// Mock Event - 改进版本
class MockEvent : public Event {
public:
    MockEvent() : Event(Event::Type::UserCustom, Timestamp::now(), {}) {
        // 在构造函数中设置默认属性
        default_attrs_["source"] = "mock";
        default_attrs_["created_by"] = "MockEvent";
        
        // 使用 ON_CALL 而不是 EXPECT_CALL
        ON_CALL(*this, attributes())
            .WillByDefault(ReturnRef(default_attrs_));
            
        ON_CALL(*this, id())
            .WillByDefault(Return(foundation::Uuid_create()));
        ON_CALL(*this, type())
            .WillByDefault(Return(Event::Type::UserCustom));
        ON_CALL(*this, timestamp())
            .WillByDefault(Return(Timestamp::now()));
        ON_CALL(*this, source())
            .WillByDefault(Return("mock_source"));
        ON_CALL(*this, payload())
            .WillByDefault(Return(nullptr));
        ON_CALL(*this, payload_type())
            .WillByDefault(Return(""));
        ON_CALL(*this, clone())
            .WillByDefault([this]() {
                auto clone = std::make_unique<NiceMock<MockEvent>>();
                ON_CALL(*clone, attributes())
                    .WillByDefault(ReturnRef(this->default_attrs_));
                return clone;
            });
    }
    
    virtual ~MockEvent() = default;
    
    // Mock 方法
    MOCK_METHOD(foundation::Uuid, id, (), (const, override));
    MOCK_METHOD(Event::Type, type, (), (const, override));
    MOCK_METHOD(Timestamp, timestamp, (), (const, override));
    MOCK_METHOD(std::string, source, (), (const, override));
    MOCK_METHOD(const void*, payload, (), (const, override));
    MOCK_METHOD(std::string, payload_type, (), (const, override));
    MOCK_METHOD(const Event::Attributes&, attributes, (), (const, override));
    MOCK_METHOD(std::unique_ptr<Event>, clone, (), (const, override));
    
    // 提供访问器以设置测试属性
    void set_test_attributes(const Event::Attributes& attrs) {
        default_attrs_ = attrs;
    }
    
private:
    Event::Attributes default_attrs_;
};
// Mock DataSource - 用于测试依赖 DataSource 的组件
class MockDataSource : public DataSource {
public:
    MockDataSource(const std::string& name, const std::string& uri) 
        : DataSource(name, uri) {}
    
    MOCK_METHOD(engine::Error, connect, (), (override));
    MOCK_METHOD(engine::Error, disconnect, (), (override));
    MOCK_METHOD(engine::Error, poll, (), (override));
    MOCK_METHOD(std::string, name, (), (const, override));
    MOCK_METHOD(std::string, uri, (), (const, override));
    MOCK_METHOD(DataListener::State, state, (), (const, override));
    MOCK_METHOD(void, register_listener, (DataListener*), (override));
    MOCK_METHOD(void, unregister_listener, (DataListener*), (override));
    MOCK_METHOD(void, set_poll_interval, (engine::Duration), (override));
};

// Mock TriggerCondition 和 TriggerAction - 用于测试
class MockTriggerCondition : public TriggerCondition {
public:
    MOCK_METHOD(bool, check, (const Event&, Timestamp), (override));
    MOCK_METHOD(std::string, description, (), (const, override));
    MOCK_METHOD(std::unique_ptr<TriggerCondition>, clone, (), (const, override));
};

class MockTriggerAction : public TriggerAction {
public:
    MOCK_METHOD(Error, execute, (const Event&, Timestamp), (override));
    MOCK_METHOD(std::string, description, (), (const, override));
    MOCK_METHOD(std::unique_ptr<TriggerAction>, clone, (), (const, override));
};

// Mock Trigger - 用于测试依赖 Trigger 的组件
class MockTrigger : public Trigger {
public:
    MockTrigger(const std::string& name, 
                std::unique_ptr<TriggerCondition> condition,
                std::unique_ptr<TriggerAction> action)
        : Trigger(name, std::move(condition), std::move(action)) {}
    
    MOCK_METHOD(Error, evaluate, (const Event&, Timestamp), (override));
    MOCK_METHOD(std::string, name, (), (const, override));
    MOCK_METHOD(foundation::Uuid, id, (), (const, override));
    MOCK_METHOD(void, set_enabled, (bool), (override));
    MOCK_METHOD(bool, is_enabled, (), (const, override));
    MOCK_METHOD(const TriggerCondition*, condition, (), (const, override));
    MOCK_METHOD(const TriggerAction*, action, (), (const, override));
};

// Mock Clock - 用于测试依赖 Clock 的组件
class MockClock : public Clock {
public:
    MOCK_METHOD(Timestamp, current_time, (), (const, override));
    MOCK_METHOD(Error, advance_to, (Timestamp), (override));
    MOCK_METHOD(Error, start, (), (override));
    MOCK_METHOD(Error, stop, (), (override));
    MOCK_METHOD(Error, reset, (Timestamp), (override));
    MOCK_METHOD(bool, is_running, (), (const, override));
    MOCK_METHOD(Clock::Mode, mode, (), (const, override));
};

// ========== 测试辅助函数 ==========

// 创建 Mock 事件的辅助函数（用于组件测试）
std::unique_ptr<Event> create_mock_event_for_test(
    Event::Type type = Event::Type::MarketData,
    foundation::Timestamp timestamp = foundation::Timestamp(),
    const std::map<std::string, std::string>& attributes = {}) {
    
    auto mock = std::make_unique<MockEvent>();
    
    static foundation::Uuid fixed_id = foundation::Uuid::generate();
    static std::map<std::string, std::string> fixed_attrs = attributes;
    
    ON_CALL(*mock, id()).WillByDefault(Return(fixed_id));
    ON_CALL(*mock, type()).WillByDefault(Return(type));
    ON_CALL(*mock, timestamp()).WillByDefault(Return(timestamp));
    ON_CALL(*mock, source()).WillByDefault(Return("test_source"));
    ON_CALL(*mock, attributes()).WillByDefault(ReturnRef(fixed_attrs));
    
    return mock;
}


TEST(MockEventTest, InheritedAttributeMethodsWork) {
    // 创建 MockEvent
    auto mock_event = std::make_unique<MockEvent>();
    
    // 使用局部变量而不是 static
    Event::Attributes test_attrs = {
        {"symbol", "AAPL"},
        {"price", "150.25"},
        {"volume", "1000"}
    };
    
    // 只设置一次期望
    EXPECT_CALL(*mock_event, attributes())
        .WillRepeatedly(ReturnRef(test_attrs));
    
    // 测试 has_attribute（继承自基类的非虚函数）
    EXPECT_TRUE(mock_event->has_attribute("symbol"));
    EXPECT_TRUE(mock_event->has_attribute("price"));
    EXPECT_FALSE(mock_event->has_attribute("nonexistent"));
    
    // 测试 get_attribute（继承自基类的非虚函数）
    std::string symbol, price, not_found;
    EXPECT_TRUE(mock_event->get_attribute("symbol", symbol));
    EXPECT_EQ(symbol, "AAPL");
    
    EXPECT_TRUE(mock_event->get_attribute("price", price));
    EXPECT_EQ(price, "150.25");
    
    EXPECT_FALSE(mock_event->get_attribute("nonexistent", not_found));
    
    // 注意：不要在设置期望后再添加新的 EXPECT_CALL
}
TEST(EventIntegrationTest, EventTypeToString) {
    const char* type_str = Event::type_to_string(Event::Type::MarketData);
    ASSERT_NE(type_str, nullptr);
    EXPECT_STREQ(type_str, "MarketData");
}

TEST(DataSourceIntegrationTest, CreateRealDataSource) {
    std::cout << "\n=== 开始测试 DataSource ===" << std::endl;
    
    try {
        // 1. 创建测试监听器
        class TestListener : public DataListener {
        public:
            void on_data_received(std::unique_ptr<Event> event) override {
                std::cout << "[TestListener] 收到数据事件" << std::endl;
                if (event) {
                    std::string symbol;
                    if (event->get_attribute("symbol", symbol)) {
                        std::cout << "[TestListener] 股票代码: " << symbol << std::endl;
                    }
                }
            }
            
            void on_state_changed(State old_state, State new_state) override {
                std::cout << "[TestListener] 状态变化: " 
                          << static_cast<int>(old_state) << " -> " 
                          << static_cast<int>(new_state) << std::endl;
            }
        };
        
        TestListener test_listener;
        
        // 2. 创建数据源
        std::cout << "[TEST] 步骤1: 创建 DataSource..." << std::endl;
        auto ds = DataSource::create("TestSource", "mock://data");
        ASSERT_NE(ds, nullptr);
        std::cout << "[TEST] ✓ DataSource 创建成功" << std::endl;
        
        // 3. 注册监听器（关键步骤！）
        std::cout << "[TEST] 步骤2: 注册监听器..." << std::endl;
        ds->register_listener(&test_listener);
        
        
        // 4. 测试基本属性
        EXPECT_EQ(ds->name(), "TestSource");
        EXPECT_EQ(ds->uri(), "mock://data");
        EXPECT_EQ(ds->state(), DataListener::State::Disconnected);
        
        // 5. 连接数据源
        
        auto connect_result = ds->connect();
        EXPECT_TRUE(connect_result.ok()) << "连接失败: " << connect_result.message;
        EXPECT_EQ(ds->state(), DataListener::State::Connected);
        std::cout << "[TEST] ✓ 连接成功" << std::endl;
        
        // 6. 等待一下，让轮询线程有机会运行
        std::cout << "[TEST] 步骤4: 等待数据..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 7. 手动调用 poll() 确保有数据
        std::cout << "[TEST] 步骤5: 手动轮询数据..." << std::endl;
        auto poll_result = ds->poll();
        EXPECT_TRUE(poll_result.ok()) << "轮询失败: " << poll_result.message;
        std::cout << "[TEST] ✓ 轮询成功" << std::endl;
        
        // 8. 断开连接
        std::cout << "[TEST] 步骤6: 断开连接..." << std::endl;
        auto disconnect_result = ds->disconnect();
        EXPECT_TRUE(disconnect_result.ok()) << "断开失败: " << disconnect_result.message;
        EXPECT_EQ(ds->state(), DataListener::State::Disconnected);
        std::cout << "[TEST] ✓ 断开成功" << std::endl;
        
        // 9. 取消注册监听器
        std::cout << "[TEST] 步骤7: 取消注册监听器..." << std::endl;
        ds->unregister_listener(&test_listener);
        
    } catch (const std::exception& e) {
        std::cerr << "[TEST] 异常: " << e.what() << std::endl;
        FAIL() << "测试异常: " << e.what();
    }
}

// 测试基础类型
TEST(FoundationIntegrationTest, UuidGeneration) {
    auto uuid1 = foundation::Uuid::generate();
    auto uuid2 = foundation::Uuid::generate();
    
    EXPECT_FALSE(uuid1.to_string().empty());
    EXPECT_FALSE(uuid2.to_string().empty());
    EXPECT_NE(uuid1.to_string(), uuid2.to_string());
}

TEST(FoundationIntegrationTest, TimestampOperations) {
    Timestamp now = Timestamp::now();
    std::string time_str = now.to_string();
    EXPECT_FALSE(time_str.empty());
    
    // 测试特定时间创建
    Timestamp specific_time("2024-01-01 09:30:00");
    EXPECT_EQ(specific_time.to_string(), "2024-01-01 09:30:00");
}

// ========== 第二部分：使用 Mock 测试组件类（单元测试） ==========

// 使用 MockEvent 测试依赖 Event 的组件
TEST(EventComponentTest, TestComponentWithMockEvent) {
    // 创建 Mock 事件
    auto mock_event = std::make_unique<MockEvent>();
    
    // 设置期望
    foundation::Uuid expected_id = foundation::Uuid::generate();
    Timestamp expected_time = Timestamp::now();
    
    EXPECT_CALL(*mock_event, id())
        .WillOnce(Return(expected_id));
    EXPECT_CALL(*mock_event, type())
        .WillOnce(Return(Event::Type::MarketData));
    EXPECT_CALL(*mock_event, timestamp())
        .WillOnce(Return(expected_time));
    
    // 假设有一个使用 Event 的组件
    // EventProcessor processor;
    // processor.process(*mock_event);
    
    // 验证 Mock 调用
    EXPECT_EQ(mock_event->id(), expected_id);
    EXPECT_EQ(mock_event->type(), Event::Type::MarketData);
    EXPECT_EQ(mock_event->timestamp(), expected_time);
}

// 使用 MockDataSource 测试依赖 DataSource 的组件
TEST(DataSourceComponentTest, TestComponentWithMockDataSource) {
    // 创建 Mock 数据源
    auto mock_ds = std::make_unique<MockDataSource>("TestSource", "mock://data");
    
    // 设置期望
    EXPECT_CALL(*mock_ds, connect())
        .WillOnce(Return(Error::success()));
    EXPECT_CALL(*mock_ds, disconnect())
        .WillOnce(Return(Error::success()));
    EXPECT_CALL(*mock_ds, name())
        .WillRepeatedly(Return("TestSource"));
    
    // 假设有一个使用 DataSource 的组件
    // DataProcessor processor(std::move(mock_ds));
    // processor.start();
    
    // 验证 Mock 调用
    
    auto connect_result = mock_ds->connect();
    EXPECT_TRUE(connect_result.ok());
    auto disconnect_result = mock_ds->disconnect();  // 添加这行
    EXPECT_TRUE(disconnect_result.ok());
    EXPECT_EQ(mock_ds->name(), "TestSource");
}

// 使用 MockTrigger 测试依赖 Trigger 的组件
// 2. 修复 TriggerComponentTest
TEST(TriggerComponentTest, TestComponentWithMockTrigger) {
    // 创建 Mock 对象
    auto mock_condition = std::make_unique<MockTriggerCondition>();
    auto mock_action = std::make_unique<MockTriggerAction>();
    
    // 使用 ON_CALL
    ON_CALL(*mock_condition, description())
        .WillByDefault(Return("Test Condition"));
    ON_CALL(*mock_condition, check(testing::_, testing::_))
        .WillByDefault(Return(true));
    
    ON_CALL(*mock_action, description())
        .WillByDefault(Return("Test Action"));
    ON_CALL(*mock_action, execute(testing::_, testing::_))
        .WillByDefault(Return(Error::success()));
    
    // 创建 Mock 触发器
    auto mock_trigger = std::make_unique<MockTrigger>(
        "TestTrigger",
        std::move(mock_condition),
        std::move(mock_action)
    );
    
    // 设置触发器期望
    ON_CALL(*mock_trigger, name())
        .WillByDefault(Return("TestTrigger"));
    ON_CALL(*mock_trigger, is_enabled())
        .WillByDefault(Return(true));
    ON_CALL(*mock_trigger, evaluate(testing::_, testing::_))
        .WillByDefault(Return(Error::success()));
    
    // 实际调用
    EXPECT_EQ(mock_trigger->name(), "TestTrigger");
    EXPECT_TRUE(mock_trigger->is_enabled());
    
    // 如果条件和方法在构造函数中被调用，这里调用它们
    // 注意：这些对象已被转移，需要通过其他方式调用
    
    SUCCEED();
}

// 测试 Event 的克隆功能
TEST(EventComponentTest, EventCloning) {
    auto mock_event = std::make_unique<MockEvent>();
    
    // 设置克隆的期望
    auto cloned_mock = std::make_unique<MockEvent>();
    EXPECT_CALL(*mock_event, clone())
        .WillOnce(Return(ByMove(std::move(cloned_mock))));
    
    // 测试克隆
    auto cloned = mock_event->clone();
    ASSERT_NE(cloned, nullptr);
}

// ========== 第三部分：集成场景测试 ==========

// 测试简单的集成场景
TEST(IntegrationScenarioTest, DataFlowTest) {
    // 1. 创建真实数据源
    auto data_source = DataSource::create("StockDataSource", "mock://stocks");
    ASSERT_NE(data_source, nullptr);
    
    // 2. 连接数据源
    auto connect_result = data_source->connect();
    EXPECT_TRUE(connect_result.ok());
    
    // 3. 创建真实事件
    auto event = Event::create(
        Event::Type::MarketData,
        Timestamp::now(),
        {{"symbol", "AAPL"}, {"price", "150.25"}}
    );
    ASSERT_NE(event, nullptr);
    
    // 4. 验证基本功能
    std::string symbol;
    EXPECT_TRUE(event->get_attribute("symbol", symbol));
    EXPECT_EQ(symbol, "AAPL");
    
    // 所有组件正常工作
    SUCCEED();
}

// ========== 第四部分：错误处理测试 ==========

TEST(ErrorHandlingTest, ErrorObject) {
    Error success = Error::success();
    EXPECT_TRUE(success.ok());
    EXPECT_EQ(success.code, 0);
    
    Error failure = Error::fail(1001, "Test error message");
    EXPECT_FALSE(failure.ok());
    EXPECT_EQ(failure.code, 1001);
    EXPECT_EQ(failure.message, "Test error message");
}

// ========== 第五部分：数据结构测试 ==========

TEST(DataStructureTest, BacktestResultStructures) {
    // 测试 TradeStats
    BacktestResult::TradeStats stats;
    stats.total_trades = 10;
    stats.winning_trades = 6;
    stats.losing_trades = 4;
    stats.win_rate = 0.6;
    stats.profit_factor = 2.5;
    stats.net_profit = 3000.0;
    
    EXPECT_EQ(stats.total_trades, 10);
    EXPECT_EQ(stats.winning_trades, 6);
    EXPECT_DOUBLE_EQ(stats.win_rate, 0.6);
    EXPECT_DOUBLE_EQ(stats.profit_factor, 2.5);
    
    // 测试 RiskMetrics
    BacktestResult::RiskMetrics risk;
    risk.max_drawdown = 0.15;
    risk.sharpe_ratio = 1.8;
    risk.volatility = 0.2;
    
    EXPECT_DOUBLE_EQ(risk.max_drawdown, 0.15);
    EXPECT_DOUBLE_EQ(risk.sharpe_ratio, 1.8);
}

// ========== 主函数 ==========
int main(int argc, char **argv) {
     // 设置控制台编码（Windows）
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    #endif
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "========================================\n" \
                 "量化引擎 GTEST 测试套件 - 重构版本\n" \
                 "区分：实际类测试 vs 组件类测试\n" \
                 "========================================\n" << std::endl;
    
    int result = RUN_ALL_TESTS();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "测试完成" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return result;
}