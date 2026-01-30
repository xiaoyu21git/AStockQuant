// // astock_engine/tests/test_eventsystem.cpp
#include <gtest/gtest.h>
// #include "EventSystem.hpp"
// #include <memory>
// #include <iostream>

// using namespace astock;

// // ===== 基础功能测试 =====

// TEST(EventSystemTest, BasicConstruction) {
//     // 测试基本构造
//     EventFormat event1("test.event", "test.source");
//     EXPECT_EQ(event1.event_type, "test.event");
//     EXPECT_EQ(event1.source, "test.source");
//     EXPECT_GT(event1.timestamp_us, 0);
    
//     // 测试带时间戳的构造
//     EventFormat event2("test.event2", "test.source2", 1234567890);
//     EXPECT_EQ(event2.timestamp_us, 1234567890);
// }

// TEST(EventSystemTest, DataAccess) {
//     EventFormat event("test", "source");
    
//     // 测试设置和获取数据
//     event.set("string_field", std::string("hello"));
//     event.set("int_field", static_cast<int64_t>(42));
//     event.set("double_field", 3.14159);
//     event.set("bool_field", true);
    
//     // 测试类型安全的获取
//     auto str_val = event.get<std::string>("string_field");
//     EXPECT_TRUE(str_val.has_value());
//     EXPECT_EQ(str_val.value(), "hello");
    
//     auto int_val = event.get<int64_t>("int_field");
//     EXPECT_TRUE(int_val.has_value());
//     EXPECT_EQ(int_val.value(), 42);
    
//     auto double_val = event.get<double>("double_field");
//     EXPECT_TRUE(double_val.has_value());
//     EXPECT_NEAR(double_val.value(), 3.14159, 1e-10);
    
//     auto bool_val = event.get<bool>("bool_field");
//     EXPECT_TRUE(bool_val.has_value());
//     EXPECT_TRUE(bool_val.value());
    
//     // 测试不存在的字段
//     auto missing = event.get<std::string>("missing");
//     EXPECT_FALSE(missing.has_value());
    
//     // 测试类型不匹配
//     auto wrong_type = event.get<int64_t>("string_field");
//     EXPECT_FALSE(wrong_type.has_value());
// }

// TEST(EventSystemTest, HasMethod) {
//     EventFormat event("test", "source");
//     event.set("field1", "value1");
    
//     EXPECT_TRUE(event.has("field1"));
//     EXPECT_FALSE(event.has("field2"));
// }

// // ===== JSON 序列化测试 =====

// TEST(EventSystemTest, JsonSerialization) {
//     // 创建测试事件
//     EventFormat event("test.event", "test.source", 1234567890000000);
//     event.set("symbol", std::string("AAPL"));
//     event.set("price", 150.25);
//     event.set("volume", static_cast<int64_t>(1000));
//     event.set("is_active", true);
    
//     // 序列化为 JSON
//     std::string json = event.to_json();
//     std::string pretty_json = event.to_json(true);
    
//     // 基本验证
//     EXPECT_FALSE(json.empty());
//     EXPECT_FALSE(pretty_json.empty());
    
//     // 验证包含关键字段
//     EXPECT_NE(json.find("test.event"), std::string::npos);
//     EXPECT_NE(json.find("test.source"), std::string::npos);
//     EXPECT_NE(json.find("1234567890000000"), std::string::npos);
//     EXPECT_NE(json.find("AAPL"), std::string::npos);
//     EXPECT_NE(json.find("150.25"), std::string::npos);
//     EXPECT_NE(json.find("1000"), std::string::npos);
//     EXPECT_NE(json.find("true"), std::string::npos);
    
//     // 反序列化测试
//     auto parsed_event = EventFormat::from_json(json);
//     EXPECT_TRUE(parsed_event.has_value());
    
//     EventFormat& parsed = parsed_event.value();
//     EXPECT_EQ(parsed.event_type, "test.event");
//     EXPECT_EQ(parsed.source, "test.source");
//     EXPECT_EQ(parsed.timestamp_us, 1234567890000000);
    
//     // 验证反序列化的数据
//     auto symbol = parsed.get<std::string>("symbol");
//     EXPECT_TRUE(symbol.has_value());
//     EXPECT_EQ(symbol.value(), "AAPL");
    
//     auto price = parsed.get<double>("price");
//     EXPECT_TRUE(price.has_value());
//     EXPECT_NEAR(price.value(), 150.25, 1e-10);
    
//     auto volume = parsed.get<int64_t>("volume");
//     EXPECT_TRUE(volume.has_value());
//     EXPECT_EQ(volume.value(), 1000);
    
//     auto is_active = parsed.get<bool>("is_active");
//     EXPECT_TRUE(is_active.has_value());
//     EXPECT_TRUE(is_active.value());
// }

// TEST(EventSystemTest, JsonRoundTrip) {
//     // 测试 JSON 往返
//     EventFormat original("roundtrip.test", "test.source");
//     original.set("field1", std::string("value1"));
//     original.set("field2", static_cast<int64_t>(999));
//     original.set("field3", 123.456);
//     original.set("field4", false);
    
//     // 序列化
//     std::string json = original.to_json();
    
//     // 反序列化
//     auto parsed = EventFormat::from_json(json);
//     EXPECT_TRUE(parsed.has_value());
    
//     // 验证相等性
//     EXPECT_EQ(original.event_type, parsed->event_type);
//     EXPECT_EQ(original.source, parsed->source);
//     EXPECT_EQ(original.timestamp_us, parsed->timestamp_us);
    
//     // 验证数据字段
//     EXPECT_EQ(original.get<std::string>("field1"), parsed->get<std::string>("field1"));
//     EXPECT_EQ(original.get<int64_t>("field2"), parsed->get<int64_t>("field2"));
//     EXPECT_EQ(original.get<double>("field3"), parsed->get<double>("field3"));
//     EXPECT_EQ(original.get<bool>("field4"), parsed->get<bool>("field4"));
// }

// TEST(EventSystemTest, InvalidJson) {
//     // 测试无效 JSON
//     EXPECT_FALSE(EventFormat::from_json("").has_value());
//     EXPECT_FALSE(EventFormat::from_json("not json").has_value());
//     EXPECT_FALSE(EventFormat::from_json("{}").has_value()); // 缺少 event_type
//     EXPECT_FALSE(EventFormat::from_json("{\"event_type\":\"test\"}").has_value()); // 缺少必要字段
// }

// // ===== 事件验证测试 =====

// TEST(EventSystemTest, Validation) {
//     // 有效事件
//     EventFormat valid_event("valid.event", "test.source");
//     valid_event.set("field", "value");
//     EXPECT_TRUE(valid_event.validate());
    
//     // 无效事件类型
//     EventFormat invalid_type("", "test.source");
//     EXPECT_FALSE(invalid_type.validate());
    
//     // 未来时间戳
//     EventFormat future_event("test", "source", 
//                             EventFormat::current_timestamp() + 2000000);
//     EXPECT_FALSE(future_event.validate());
    
//     // 太古老的时间戳
//     EventFormat ancient_event("test", "source", 1000000);
//     EXPECT_FALSE(ancient_event.validate());
    
//     // 无效的浮点数
//     EventFormat nan_event("test", "source");
//     nan_event.set("field", std::numeric_limits<double>::quiet_NaN());
//     EXPECT_FALSE(nan_event.validate());
    
//     // 无限大的浮点数
//     EventFormat inf_event("test", "source");
//     inf_event.set("field", std::numeric_limits<double>::infinity());
//     EXPECT_FALSE(inf_event.validate());
    
//     // 空键
//     EventFormat empty_key("test", "source");
//     empty_key.set("", "value");
//     EXPECT_FALSE(empty_key.validate());
// }

// // ===== 事件比较测试 =====

// TEST(EventSystemTest, Equality) {
//     EventFormat event1("test", "source", 1234567890);
//     event1.set("field1", "value1");
//     event1.set("field2", 42);
    
//     // 相同的事件
//     EventFormat event2("test", "source", 1234567890);
//     event2.set("field1", "value1");
//     event2.set("field2", 42);
    
//     EXPECT_TRUE(event1 == event2);
//     EXPECT_FALSE(event1 != event2);
    
//     // 不同的事件类型
//     EventFormat event3("different", "source", 1234567890);
//     event3.set("field1", "value1");
//     EXPECT_TRUE(event1 != event3);
    
//     // 不同的时间戳
//     EventFormat event4("test", "source", 1234567891);
//     event4.set("field1", "value1");
//     EXPECT_TRUE(event1 != event4);
    
//     // 不同的数据字段
//     EventFormat event5("test", "source", 1234567890);
//     event5.set("field1", "value2");
//     EXPECT_TRUE(event1 != event5);
    
//     // 浮点数比较容差
//     EventFormat event6("test", "source", 1234567890);
//     event6.set("double_field", 1.0);
    
//     EventFormat event7("test", "source", 1234567890);
//     event7.set("double_field", 1.0000000001);
    
//     EXPECT_TRUE(event6 == event7); // 应该在容差范围内
// }

// // ===== 事件合并测试 =====

// TEST(EventSystemTest, Merge) {
//     EventFormat base("test", "source", 1000);
//     base.set("field1", "value1");
//     base.set("field2", 100);
    
//     EventFormat other("test", "source", 2000); // 时间戳不应该被合并
//     other.set("field2", 200); // 覆盖 field2
//     other.set("field3", "new_value"); // 新增 field3
    
//     EventFormat merged = base.merge(other);
    
//     EXPECT_EQ(merged.event_type, "test");
//     EXPECT_EQ(merged.source, "source");
//     EXPECT_EQ(merged.timestamp_us, 1000); // 保持原时间戳
    
//     EXPECT_EQ(merged.get<std::string>("field1").value(), "value1");
//     EXPECT_EQ(merged.get<int64_t>("field2").value(), 200); // 被覆盖
//     EXPECT_EQ(merged.get<std::string>("field3").value(), "new_value");
// }

// // ===== 事件转换测试 =====

// TEST(EventSystemTest, Transform) {
//     EventFormat original("test", "source");
//     original.set("value", 10);
    
//     // 转换：将值加倍
//     EventFormat transformed = original.transform([](EventFormat& event) {
//         auto value = event.get<int64_t>("value");
//         if (value) {
//             event.set("value", value.value() * 2);
//             event.set("transformed", true);
//         }
//     });
    
//     EXPECT_EQ(transformed.get<int64_t>("value").value(), 20);
//     EXPECT_TRUE(transformed.get<bool>("transformed").value());
    
//     // 原始事件不应被修改
//     EXPECT_EQ(original.get<int64_t>("value").value(), 10);
//     EXPECT_FALSE(original.has("transformed"));
// }

// // ===== 工厂方法测试 =====

// TEST(EventFactoryTest, CreateMarketTick) {
//     auto event = EventFactory::create_market_tick(
//         "AAPL", 150.25, 1000, 150.20, 150.30, 500, 300);
    
//     EXPECT_EQ(event.event_type, EventType::MARKET_TICK);
//     EXPECT_EQ(event.source, EventSource::MARKET_DATA);
    
//     EXPECT_EQ(event.get<std::string>("symbol").value(), "AAPL");
//     EXPECT_EQ(event.get<double>("price").value(), 150.25);
//     EXPECT_EQ(event.get<int64_t>("volume").value(), 1000);
//     EXPECT_EQ(event.get<double>("bid").value(), 150.20);
//     EXPECT_EQ(event.get<double>("ask").value(), 150.30);
//     EXPECT_EQ(event.get<int64_t>("bid_size").value(), 500);
//     EXPECT_EQ(event.get<int64_t>("ask_size").value(), 300);
// }

// TEST(EventFactoryTest, CreateMarketBar) {
//     auto event = EventFactory::create_market_bar(
//         "AAPL", "1m", 150.0, 151.0, 149.5, 150.5, 10000, 1234567890000000);
    
//     EXPECT_EQ(event.event_type, "market.bar.1m");
//     EXPECT_EQ(event.source, EventSource::MARKET_DATA);
//     EXPECT_EQ(event.timestamp_us, 1234567890000000);
    
//     EXPECT_EQ(event.get<std::string>("symbol").value(), "AAPL");
//     EXPECT_EQ(event.get<double>("open").value(), 150.0);
//     EXPECT_EQ(event.get<double>("high").value(), 151.0);
//     EXPECT_EQ(event.get<double>("low").value(), 149.5);
//     EXPECT_EQ(event.get<double>("close").value(), 150.5);
//     EXPECT_EQ(event.get<int64_t>("volume").value(), 10000);
//     EXPECT_EQ(event.get<std::string>("interval").value(), "1m");
// }

// TEST(EventFactoryTest, CreateOrderEvent) {
//     auto event = EventFactory::create_order_event(
//         "order123", "AAPL", "BUY", "LIMIT", 150.0, 100, "NEW", "account1");
    
//     EXPECT_EQ(event.event_type, EventType::ORDER_NEW);
//     EXPECT_EQ(event.source, EventSource::TRADING);
    
//     EXPECT_EQ(event.get<std::string>("order_id").value(), "order123");
//     EXPECT_EQ(event.get<std::string>("symbol").value(), "AAPL");
//     EXPECT_EQ(event.get<std::string>("side").value(), "BUY");
//     EXPECT_EQ(event.get<std::string>("order_type").value(), "LIMIT");
//     EXPECT_EQ(event.get<double>("price").value(), 150.0);
//     EXPECT_EQ(event.get<int64_t>("quantity").value(), 100);
//     EXPECT_EQ(event.get<std::string>("status").value(), "NEW");
//     EXPECT_EQ(event.get<std::string>("account").value(), "account1");
// }

// TEST(EventFactoryTest, CreateStrategySignal) {
//     auto event = EventFactory::create_strategy_signal(
//         "strategy1", "AAPL", "BUY", 0.8, 150.0, 100);
    
//     EXPECT_EQ(event.event_type, EventType::STRATEGY_SIGNAL);
//     EXPECT_EQ(event.source, EventSource::STRATEGY);
    
//     EXPECT_EQ(event.get<std::string>("strategy_id").value(), "strategy1");
//     EXPECT_EQ(event.get<std::string>("symbol").value(), "AAPL");
//     EXPECT_EQ(event.get<std::string>("signal").value(), "BUY");
//     EXPECT_EQ(event.get<double>("strength").value(), 0.8);
//     EXPECT_EQ(event.get<double>("price").value(), 150.0);
//     EXPECT_EQ(event.get<int64_t>("quantity").value(), 100);
// }

// TEST(EventFactoryTest, CreateSystemEvents) {
//     // 系统启动事件
//     auto startup = EventFactory::create_system_startup("1.0.0", "localhost");
//     EXPECT_EQ(startup.event_type, EventType::SYSTEM_STARTUP);
//     EXPECT_EQ(startup.source, EventSource::SYSTEM);
//     EXPECT_EQ(startup.get<std::string>("version").value(), "1.0.0");
//     EXPECT_EQ(startup.get<std::string>("hostname").value(), "localhost");
    
//     // 系统错误事件
//     auto error = EventFactory::create_system_error("database", "Connection failed", 1001);
//     EXPECT_EQ(error.event_type, EventType::SYSTEM_ERROR);
//     EXPECT_EQ(error.source, EventSource::SYSTEM);
//     EXPECT_EQ(error.get<std::string>("component").value(), "database");
//     EXPECT_EQ(error.get<std::string>("error_message").value(), "Connection failed");
//     EXPECT_EQ(error.get<int64_t>("error_code").value(), 1001);
    
//     // 心跳事件
//     std::unordered_map<std::string, EventValue> metrics = {
//         {"cpu_usage", 0.5},
//         {"memory_mb", 1024.0}
//     };
//     auto heartbeat = EventFactory::create_heartbeat("engine", metrics);
//     EXPECT_EQ(heartbeat.event_type, EventType::SYSTEM_HEARTBEAT);
//     EXPECT_EQ(heartbeat.source, EventSource::SYSTEM);
//     EXPECT_EQ(heartbeat.get<std::string>("component").value(), "engine");
//     EXPECT_EQ(heartbeat.get<double>("cpu_usage").value(), 0.5);
//     EXPECT_EQ(heartbeat.get<double>("memory_mb").value(), 1024.0);
// }

// TEST(EventFactoryTest, CreateTradeEvents) {
//     // 订单成交事件
//     auto filled = EventFactory::create_order_filled(
//         "order123", "AAPL", 150.25, 100, 1.50);
//     EXPECT_EQ(filled.event_type, EventType::ORDER_FILLED);
//     EXPECT_EQ(filled.get<std::string>("order_id").value(), "order123");
//     EXPECT_EQ(filled.get<double>("fill_price").value(), 150.25);
//     EXPECT_EQ(filled.get<int64_t>("fill_quantity").value(), 100);
//     EXPECT_EQ(filled.get<double>("commission").value(), 1.50);
    
//     // 订单拒绝事件
//     auto rejected = EventFactory::create_order_rejected(
//         "order456", "AAPL", "Insufficient funds", "REJECT_001");
//     EXPECT_EQ(rejected.event_type, EventType::ORDER_REJECTED);
//     EXPECT_EQ(rejected.get<std::string>("reject_reason").value(), "Insufficient funds");
//     EXPECT_EQ(rejected.get<std::string>("reject_code").value(), "REJECT_001");
// }

// TEST(EventFactoryTest, CreateStrategyPositionUpdate) {
//     auto event = EventFactory::create_strategy_position_update(
//         "strategy1", "AAPL", 100, 150.0, 15500.0, 500.0);
    
//     EXPECT_EQ(event.event_type, EventType::STRATEGY_POSITION_UPDATE);
//     EXPECT_EQ(event.get<std::string>("strategy_id").value(), "strategy1");
//     EXPECT_EQ(event.get<std::string>("symbol").value(), "AAPL");
//     EXPECT_EQ(event.get<int64_t>("position").value(), 100);
//     EXPECT_EQ(event.get<double>("avg_price").value(), 150.0);
//     EXPECT_EQ(event.get<double>("market_value").value(), 15500.0);
//     EXPECT_EQ(event.get<double>("unrealized_pnl").value(), 500.0);
// }

// TEST(EventFactoryTest, CreateRiskWarning) {
//     auto event = EventFactory::create_risk_warning(
//         "rule001", "Position Limit", "AAPL", 110000.0, 100000.0, "HIGH");
    
//     EXPECT_EQ(event.event_type, EventType::RISK_WARNING);
//     EXPECT_EQ(event.get<std::string>("rule_id").value(), "rule001");
//     EXPECT_EQ(event.get<std::string>("rule_name").value(), "Position Limit");
//     EXPECT_EQ(event.get<std::string>("symbol").value(), "AAPL");
//     EXPECT_EQ(event.get<double>("current_value").value(), 110000.0);
//     EXPECT_EQ(event.get<double>("threshold").value(), 100000.0);
//     EXPECT_EQ(event.get<std::string>("severity").value(), "HIGH");
//     EXPECT_NEAR(event.get<double>("exceed_ratio").value(), 10.0, 1e-10);
// }

// TEST(EventFactoryTest, CreateBacktestProgress) {
//     auto event = EventFactory::create_backtest_progress(
//         "backtest123", "strategy1", 0.5, 5000, 10000, 2500.0);
    
//     EXPECT_EQ(event.event_type, EventType::BACKTEST_PROGRESS);
//     EXPECT_EQ(event.get<std::string>("backtest_id").value(), "backtest123");
//     EXPECT_EQ(event.get<std::string>("strategy_id").value(), "strategy1");
//     EXPECT_EQ(event.get<double>("progress").value(), 0.5);
//     EXPECT_EQ(event.get<int64_t>("current_index").value(), 5000);
//     EXPECT_EQ(event.get<int64_t>("total_records").value(), 10000);
//     EXPECT_EQ(event.get<double>("current_pnl").value(), 2500.0);
// }

// // ===== 事件流测试 =====

// TEST(EventStreamTest, BasicOperations) {
//     EventStream stream(5); // 最大容量 5
    
//     // 添加事件
//     for (int i = 0; i < 10; i++) {
//         EventFormat event("test.event", "source");
//         event.set("index", static_cast<int64_t>(i));
//         stream.push(event);
//     }
    
//     // 验证大小（应该被限制为5）
//     EXPECT_EQ(stream.size(), 5);
    
//     // 获取所有事件
//     auto events = stream.get_events();
//     EXPECT_EQ(events.size(), 5);
    
//     // 验证事件索引（应该是最新的5个）
//     for (size_t i = 0; i < events.size(); i++) {
//         EXPECT_EQ(events[i].get<int64_t>("index").value(), 9 - i);
//     }
    
//     // 清空流
//     stream.clear();
//     EXPECT_EQ(stream.size(), 0);
//     EXPECT_TRUE(stream.get_events().empty());
// }

// TEST(EventStreamTest, Filtering) {
//     EventStream stream;
    
//     // 添加不同类型的事件
//     for (int i = 0; i < 10; i++) {
//         EventFormat event(i % 2 == 0 ? "type.a" : "type.b", "source");
//         event.set("value", static_cast<int64_t>(i));
//         stream.push(event);
//     }
    
//     // 按类型过滤
//     auto type_a_events = stream.filter_by_type("type.a");
//     EXPECT_EQ(type_a_events.size(), 5);
//     for (const auto& event : type_a_events) {
//         int64_t value = event.get<int64_t>("value").value();
//         EXPECT_EQ(value % 2, 0);
//     }
    
//     // 按值过滤
//     auto high_value_events = stream.filter([](const EventFormat& event) {
//         auto value = event.get<int64_t>("value");
//         return value && value.value() > 5;
//     });
//     EXPECT_EQ(high_value_events.size(), 4);
    
//     // 按时间过滤
//     auto events = stream.get_events();
//     if (!events.empty()) {
//         auto first_time = events.front().timestamp_us;
//         auto last_time = events.back().timestamp_us;
        
//         auto time_filtered = stream.filter_by_time(first_time, last_time);
//         EXPECT_EQ(time_filtered.size(), events.size());
//     }
    
//     // 按字段值过滤
//     EventFormat target_event("type.a", "source");
//     target_event.set("value", static_cast<int64_t>(2));
    
//     auto field_filtered = stream.filter_by_field("value", static_cast<int64_t>(2));
//     EXPECT_FALSE(field_filtered.empty());
//     for (const auto& event : field_filtered) {
//         EXPECT_EQ(event.get<int64_t>("value").value(), 2);
//     }
// }

// // ===== 事件模式匹配测试 =====

// TEST(EventPatternMatcherTest, BasicMatching) {
//     EventPatternMatcher matcher("test.event");
    
//     // 添加必需字段
//     matcher.add_required_field("field1", std::string("value1"));
//     matcher.add_required_field("field2", static_cast<int64_t>(42));
    
//     // 添加可选字段
//     matcher.add_optional_field("field3", true);
    
//     // 创建匹配的事件
//     EventFormat matching_event("test.event", "source");
//     matching_event.set("field1", std::string("value1"));
//     matching_event.set("field2", static_cast<int64_t>(42));
//     matching_event.set("field3", true);
    
//     EXPECT_TRUE(matcher.matches(matching_event));
    
//     // 缺少必需字段
//     EventFormat missing_field("test.event", "source");
//     missing_field.set("field1", std::string("value1"));
//     EXPECT_FALSE(matcher.matches(missing_field));
    
//     // 字段值不匹配
//     EventFormat wrong_value("test.event", "source");
//     wrong_value.set("field1", std::string("value1"));
//     wrong_value.set("field2", static_cast<int64_t>(99));
//     EXPECT_FALSE(matcher.matches(wrong_value));
    
//     // 可选字段不匹配（如果存在必须匹配）
//     EventFormat wrong_optional("test.event", "source");
//     wrong_optional.set("field1", std::string("value1"));
//     wrong_optional.set("field2", static_cast<int64_t>(42));
//     wrong_optional.set("field3", false);
//     EXPECT_FALSE(matcher.matches(wrong_optional));
    
//     // 事件类型不匹配
//     EventFormat wrong_type("wrong.event", "source");
//     wrong_type.set("field1", std::string("value1"));
//     wrong_type.set("field2", static_cast<int64_t>(42));
//     EXPECT_FALSE(matcher.matches(wrong_type));
// }

// TEST(EventPatternMatcherTest, FindMatches) {
//     EventPatternMatcher matcher("order.new");
//     matcher.add_required_field("side", std::string("BUY"));
//     matcher.add_required_field("status", std::string("NEW"));
    
//     std::vector<EventFormat> events = {
//         EventFactory::create_order_event("order1", "AAPL", "BUY", "LIMIT", 150.0, 100, "NEW"),
//         EventFactory::create_order_event("order2", "AAPL", "SELL", "LIMIT", 150.0, 100, "NEW"),
//         EventFactory::create_order_event("order3", "AAPL", "BUY", "LIMIT", 150.0, 100, "CANCELLED"),
//         EventFactory::create_order_event("order4", "GOOGL", "BUY", "MARKET", 2800.0, 50, "NEW"),
//     };
    
//     auto matches = matcher.find_matches(events);
//     EXPECT_EQ(matches.size(), 2); // order1 和 order4
    
//     for (const auto& match : matches) {
//         EXPECT_EQ(match.get<std::string>("side").value(), "BUY");
//         EXPECT_EQ(match.get<std::string>("status").value(), "NEW");
//     }
// }

// // ===== 事件序列化工具测试 =====

// TEST(EventSerializerTest, BinarySerialization) {
//     // 创建测试事件
//     EventFormat original("test.event", "test.source", 1234567890000000);
//     original.set("string_field", std::string("hello world"));
//     original.set("int_field", static_cast<int64_t>(-123456789));
//     original.set("double_field", 3.141592653589793);
//     original.set("bool_field", true);
    
//     // 序列化为二进制
//     auto binary = EventSerializer::to_binary(original);
//     EXPECT_FALSE(binary.empty());
    
//     // 反序列化
//     auto parsed = EventSerializer::from_binary(binary);
//     EXPECT_TRUE(parsed.has_value());
    
//     // 验证数据
//     EventFormat& restored = parsed.value();
//     EXPECT_EQ(restored.event_type, original.event_type);
//     EXPECT_EQ(restored.source, original.source);
//     EXPECT_EQ(restored.timestamp_us, original.timestamp_us);
    
//     EXPECT_EQ(restored.get<std::string>("string_field").value(), "hello world");
//     EXPECT_EQ(restored.get<int64_t>("int_field").value(), -123456789);
//     EXPECT_NEAR(restored.get<double>("double_field").value(), 3.141592653589793, 1e-15);
//     EXPECT_TRUE(restored.get<bool>("bool_field").value());
// }

// TEST(EventSerializerTest, InvalidBinary) {
//     // 测试无效二进制数据
//     std::vector<uint8_t> empty;
//     EXPECT_FALSE(EventSerializer::from_binary(empty).has_value());
    
//     std::vector<uint8_t> garbage = {0x01, 0x02, 0x03, 0x04};
//     EXPECT_FALSE(EventSerializer::from_binary(garbage).has_value());
    
//     // 测试不支持的版本
//     std::vector<uint8_t> wrong_version = {
//         0x99, 0x99, 0x99, 0x99,  // 无效版本
//         0x00, 0x00, 0x00, 0x00   // 其他数据...
//     };
//     EXPECT_FALSE(EventSerializer::from_binary(wrong_version).has_value());
// }

// // ===== 综合集成测试 =====

// TEST(IntegrationTest, EventWorkflow) {
//     // 1. 创建事件流
//     EventStream stream(100);
    
//     // 2. 生成市场数据事件
//     for (int i = 0; i < 50; i++) {
//         auto tick = EventFactory::create_market_tick(
//             "AAPL", 150.0 + i * 0.1, 1000 + i * 100);
//         stream.push(tick);
//     }
    
//     // 3. 生成策略信号事件
//     auto signal = EventFactory::create_strategy_signal(
//         "moving_average", "AAPL", "BUY", 0.75, 155.0, 100);
//     stream.push(signal);
    
//     // 4. 生成订单事件
//     auto order = EventFactory::create_order_event(
//         "order001", "AAPL", "BUY", "LIMIT", 155.0, 100, "NEW");
//     stream.push(order);
    
//     // 5. 过滤特定类型的事件
//     auto market_events = stream.filter_by_type(EventType::MARKET_TICK);
//     EXPECT_EQ(market_events.size(), 50);
    
//     auto signal_events = stream.filter_by_type(EventType::STRATEGY_SIGNAL);
//     EXPECT_EQ(signal_events.size(), 1);
    
//     // 6. 序列化和反序列化
//     if (!signal_events.empty()) {
//         std::string json = signal_events[0].to_json();
//         auto parsed = EventFormat::from_json(json);
//         EXPECT_TRUE(parsed.has_value());
//         EXPECT_TRUE(parsed->validate());
//     }
    
//     // 7. 创建模式匹配器查找特定订单
//     EventPatternMatcher order_matcher(EventType::ORDER_NEW);
//     order_matcher.add_required_field("symbol", std::string("AAPL"));
//     order_matcher.add_required_field("side", std::string("BUY"));
    
//     auto all_events = stream.get_events();
//     auto buy_orders = order_matcher.find_matches(all_events);
//     EXPECT_FALSE(buy_orders.empty());
// }

// // ===== 性能测试 =====

// TEST(PerformanceTest, EventCreation) {
//     const int NUM_EVENTS = 10000;
    
//     auto start = std::chrono::high_resolution_clock::now();
    
//     for (int i = 0; i < NUM_EVENTS; i++) {
//         EventFormat event("test.event", "source");
//         event.set("index", static_cast<int64_t>(i));
//         event.set("price", 100.0 + i * 0.01);
//         event.set("volume", static_cast<int64_t>(1000 + i));
//         event.set("active", i % 2 == 0);
        
//         // 序列化和反序列化
//         std::string json = event.to_json();
//         auto parsed = EventFormat::from_json(json);
//         EXPECT_TRUE(parsed.has_value());
//     }
    
//     auto end = std::chrono::high_resolution_clock::now();
//     auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
//     std::cout << "Created " << NUM_EVENTS << " events in " 
//               << duration.count() << " ms" << std::endl;
//     std::cout << "Average time per event: " 
//               << duration.count() * 1000.0 / NUM_EVENTS << " microseconds" << std::endl;
// }

// // ===== 边界条件测试 =====

// TEST(BoundaryTest, LargeData) {
//     // 测试大数据字段
//     EventFormat event("large.data", "test");
    
//     // 大字符串
//     std::string large_string(65536, 'X'); // 64KB
//     event.set("large_field", large_string);
//     EXPECT_TRUE(event.validate());
    
//     // 极大/极小的数字
//     event.set("max_int", std::numeric_limits<int64_t>::max());
//     event.set("min_int", std::numeric_limits<int64_t>::min());
//     event.set("max_double", std::numeric_limits<double>::max());
//     event.set("min_double", std::numeric_limits<double>::lowest());
//     EXPECT_TRUE(event.validate());
    
//     // 序列化大事件
//     std::string json = event.to_json();
//     EXPECT_FALSE(json.empty());
    
//     // 反序列化大事件
//     auto parsed = EventFormat::from_json(json);
//     EXPECT_TRUE(parsed.has_value());
//     EXPECT_EQ(parsed->get<std::string>("large_field").value(), large_string);
// }

// TEST(BoundaryTest, UnicodeAndSpecialCharacters) {
//     EventFormat event("unicode.test", "source");
    
//     // Unicode 字符
//     event.set("chinese", std::string("你好世界"));
//     event.set("emoji", std::string("🚀📈💹"));
//     event.set("special", std::string("line1\nline2\ttab\"quote\\backslash"));
    
//     // JSON 序列化应该正确处理这些字符
//     std::string json = event.to_json();
//     auto parsed = EventFormat::from_json(json);
//     EXPECT_TRUE(parsed.has_value());
    
//     EXPECT_EQ(parsed->get<std::string>("chinese").value(), "你好世界");
//     EXPECT_EQ(parsed->get<std::string>("emoji").value(), "🚀📈💹");
//     EXPECT_EQ(parsed->get<std::string>("special").value(), "line1\nline2\ttab\"quote\\backslash");
// }

// 主函数
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // 可选：设置日志级别
    // foundation::Foundation::instance().logger().set_level(foundation::log::LogLevel::WARNING);
    
    return RUN_ALL_TESTS();
}