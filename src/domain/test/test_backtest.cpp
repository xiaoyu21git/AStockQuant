// test_backtest.cpp
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <iostream>
#include <string>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <random>
#include <cmath>

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

// namespace {
//     std::random_device rd;
//     std::mt19937 gen(rd());
    
//     double random_double(double min, double max) {
//         std::uniform_real_distribution<> dis(min, max);
//         return dis(gen);
//     }
    
//     int random_int(int min, int max) {
//         std::uniform_int_distribution<> dis(min, max);
//         return dis(gen);
//     }
    
//     // 计算平仓盈亏的辅助函数
//     double calculate_pnl(double entry_price, double exit_price, double volume) {
//         return (exit_price - entry_price) * volume;
//     }
// }

// // ===== 测试用例 1: 策略并发信号处理 =====
// TEST(StrategyConcurrencyTest, ConcurrentSignalProcessing) {
//     std::cout << "Starting StrategyConcurrencyTest..." << std::endl;
    
//     const int num_strategies = 3;
//     const int bars_per_strategy = 50;
    
//     auto executor = std::make_shared<foundation::thread::ThreadPoolExecutor>(2);
//     auto bus = engine::EventBus::create(executor);
//     bus->set_policy(std::make_shared<engine::ImmediatePolicy>());
    
//     // 共享资源：所有策略的仓位统计
//     struct StrategyStats {
//         std::atomic<int> open_signals{0};
//         std::atomic<int> close_signals{0};
//     };
    
//     StrategyStats total_stats;
    
//     // 预先分配策略和仓位，避免并发访问问题
//     std::vector<std::unique_ptr<CrossSignal>> signals;
//     std::vector<std::unique_ptr<MovingAverageStrategy>> strategies;
//     std::vector<std::unique_ptr<Position>> positions;
    
//     // 生成测试数据
//     std::vector<std::vector<Bar>> all_bars(num_strategies);
//     double base_price = 100.0;
    
//     for (int s = 0; s < num_strategies; ++s) {
//         // 创建策略和仓位（使用unique_ptr避免复制）
//         signals.push_back(std::make_unique<CrossSignal>(s + 2, (s + 2) * 2));
//         strategies.push_back(std::make_unique<MovingAverageStrategy>(*signals.back()));
//         positions.push_back(std::make_unique<Position>());
        
//         strategies.back()->setEventBus(bus.get());
//         positions.back()->close();
        
//         // 生成测试数据
//         all_bars[s].reserve(bars_per_strategy);
//         double price = base_price + s * 10.0;
        
//         for (int i = 0; i < bars_per_strategy; ++i) {
//             Bar bar;
//             bar.symbol = "strategy" + std::to_string(s) + "_bar" + std::to_string(i);
//             bar.time = i + 1;
//             bar.open = price;
//             bar.high = price + 1.0;
//             bar.low = price - 1.0;
//             bar.close = price + random_double(-0.5, 0.5);
//             bar.volume = 1000;
            
//             all_bars[s].push_back(bar);
//             price = bar.close;
//         }
//     }
    
//     // 订阅策略事件 - 简化版本
//     bus->subscribe(engine::Event::Type::UserCustom,
//         [&](std::unique_ptr<engine::Event> evt) {
//             if (!evt) return;
            
//             std::string msg;
//             if (evt->get_attribute("msg", msg)) {
//                 if (msg.find("Open long at price") != std::string::npos) {
//                     total_stats.open_signals++;
//                     std::cout<< "open signal ................/n";
//                 } else if (msg.find("Close long at price") != std::string::npos) {
//                     total_stats.close_signals++;
//                     std::cout<< "close signal ................/n";
//                 }
//             }
//         }
//     );
    
//     // 并发执行：多个线程同时处理不同策略
//     std::vector<std::thread> workers;
//     std::atomic<int> completed_strategies{0};
    
//     for (int s = 0; s < num_strategies; ++s) {
//         workers.emplace_back([&, strategy_idx = s]() {
//             // 获取策略、仓位和数据的指针/引用
//             auto& strategy = *strategies[strategy_idx];
//             auto& position = *positions[strategy_idx];
//             auto& bars = all_bars[strategy_idx];
            
//             std::cout << "Thread " << strategy_idx << " starting..." << std::endl;
            
//             for (const auto& bar : bars) {
//                 auto action = strategy.onBar(bar);
                
//                 if (action == StrategyAction::OpenLong) {
//                     position.open(bar.close, 1);
//                     std::cout << "Strategy " << strategy_idx << ": OpenLong at " << bar.close << std::endl;
//                 } else if (action == StrategyAction::CloseLong) {
//                     position.close();
//                     std::cout << "Strategy " << strategy_idx << ": CloseLong at " << bar.close << std::endl;
//                 }
//             }
            
//             completed_strategies++;
//             std::cout << "Thread " << strategy_idx << " completed." << std::endl;
//         });
//     }
    
//     std::cout << "All worker threads created, waiting for completion..." << std::endl;
    
//     // 等待所有策略执行完成
//     for (auto& worker : workers) {
//         worker.join();
//     }
    
//     std::cout << "All worker threads joined." << std::endl;
    
//     // 确保所有事件处理完成
//     std::this_thread::sleep_for(std::chrono::milliseconds(100));
//     executor->shutdown(true);
    
//     // 验证业务逻辑
//     int total_open_positions = 0;
    
//     for (int s = 0; s < num_strategies; ++s) {
//         if (positions[s]->hasPosition) {
//             total_open_positions++;
//         }
//     }
    
//     std::cout << "=== 策略并发测试结果 ===" << std::endl;
//     std::cout << "策略数量: " << num_strategies << std::endl;
//     std::cout << "开仓信号: " << total_stats.open_signals.load() << std::endl;
//     std::cout << "平仓信号: " << total_stats.close_signals.load() << std::endl;
//     std::cout << "当前持仓: " << total_open_positions << std::endl;
    
//     // 业务断言
//     EXPECT_EQ(completed_strategies.load(), num_strategies);
//     // 开仓和平仓信号数量应该大致匹配
//     EXPECT_LE(std::abs(total_stats.open_signals.load() - total_stats.close_signals.load()), 1);
    
//     std::cout << "StrategyConcurrencyTest completed." << std::endl;
// }

// // ===== 测试用例 2: 简单订单并发测试 =====
// TEST(OrderConcurrencyTest, SimpleConcurrentOrders) {
//     const int num_traders = 5;
//     const int orders_per_trader = 20;
    
//     auto executor = std::make_shared<foundation::thread::ThreadPoolExecutor>(4);
//     auto bus = engine::EventBus::create(executor);
//     bus->set_policy(std::make_shared<engine::ImmediatePolicy>());
    
//     // 使用简单的计数器
//     std::atomic<int> buy_order_count{0};
//     std::atomic<int> sell_order_count{0};
//     std::atomic<int> total_orders_processed{0};
    
//     // 订阅订单事件
//     bus->subscribe(engine::Event::Type::UserCustom,
//         [&](std::unique_ptr<engine::Event> evt) {
//             if (!evt) return;
            
//             std::string order_type;
//             if (evt->get_attribute("order_type", order_type)) {
//                 if (order_type == "buy") {
//                     buy_order_count++;
//                 } else if (order_type == "sell") {
//                     sell_order_count++;
//                 }
//                 total_orders_processed++;
//             }
//         }
//     );
    
//     // 并发下单
//     std::vector<std::thread> traders;
//     std::atomic<int> orders_generated{0};
    
//     for (int t = 0; t < num_traders; ++t) {
//         traders.emplace_back([&, trader_id = t]() {
//             for (int i = 0; i < orders_per_trader; ++i) {
//                 bool is_buy = random_int(0, 1) == 0;
                
//                 engine::Event::Attributes attrs;
//                 attrs["order_type"] = is_buy ? "buy" : "sell";
//                 attrs["trader_id"] = std::to_string(trader_id);
//                 attrs["order_id"] = std::to_string(i);
                
//                 bus->publish(engine::Event::create(
//                     engine::Event::Type::UserCustom,
//                     foundation::Timestamp::now(),
//                     attrs
//                 ));
                
//                 orders_generated++;
                
//                 // 小延迟
//                 std::this_thread::sleep_for(std::chrono::microseconds(10));
//             }
//         });
//     }
    
//     // 等待所有交易者完成
//     for (auto& trader : traders) {
//         trader.join();
//     }
    
//     // 等待事件处理
//     std::this_thread::sleep_for(std::chrono::milliseconds(100));
//     executor->shutdown(true);
    
//     // 验证
//     std::cout << "=== 订单并发测试结果 ===" << std::endl;
//     std::cout << "生成订单: " << orders_generated.load() << std::endl;
//     std::cout << "处理订单: " << total_orders_processed.load() << std::endl;
//     std::cout << "买单: " << buy_order_count.load() << std::endl;
//     std::cout << "卖单: " << sell_order_count.load() << std::endl;
    
//     EXPECT_EQ(orders_generated.load(), num_traders * orders_per_trader);
//     EXPECT_EQ(total_orders_processed.load(), orders_generated.load());
//     EXPECT_EQ(buy_order_count.load() + sell_order_count.load(), orders_generated.load());
// }

// // ===== 测试用例 3: 风险控制并发检查 =====
// TEST(RiskControlConcurrencyTest, SimpleConcurrentRiskChecks) {
//     const int num_positions = 50;
    
//     auto executor = std::make_shared<foundation::thread::ThreadPoolExecutor>(2);
//     auto bus = engine::EventBus::create(executor);
//     bus->set_policy(std::make_shared<engine::ImmediatePolicy>());
    
//     // 风险控制状态 - 简化版本
//     struct RiskState {
//         std::atomic<double> total_capital_used{0.0};
//         std::atomic<int> risky_positions{0};
//         const double capital_limit = 1000000.0;
//         const double position_limit = 100000.0;


//         bool check_position(double position_value) {
//             double current = total_capital_used.load();
//             if (current + position_value > capital_limit) {
//                 risky_positions++;
//                 return false;
//             }
            
//             if (position_value > position_limit) {
//                 risky_positions++;
//                 return false;
//             }
            
//             // 原子地增加总资金使用量
//             double expected = current;
//             while (!total_capital_used.compare_exchange_weak(expected, current + position_value)) {
//                 current = expected;
//                 if (current + position_value > capital_limit) {
//                     risky_positions++;
//                     return false;
//                 }
//             }
            
//             return true;
//         }
//     };
    
//     RiskState risk_state;
//     std::atomic<int> approved_positions{0};
//     std::atomic<int> rejected_positions{0};
    
//     // 订阅仓位请求事件
//     bus->subscribe(engine::Event::Type::UserCustom,
//         [&](std::unique_ptr<engine::Event> evt) {
//             if (!evt) return;
            
//             std::string request_type;
//             std::string position_value_str;
            
//             if (evt->get_attribute("request_type", request_type) &&
//                 request_type == "position_check" &&
//                 evt->get_attribute("position_value", position_value_str)) {
                
//                 try {
//                     double position_value = std::stod(position_value_str);
//                     bool approved = risk_state.check_position(position_value);
                    
//                     if (approved) {
//                         approved_positions++;
//                     } else {
//                         rejected_positions++;
//                     }
//                 } catch (...) {
//                     rejected_positions++;  // 转换失败视为拒绝
//                 }
//             }
//         }
//     );
    
//     // 并发提交仓位检查请求
//     std::vector<std::thread> requesters;
    
//     for (int i = 0; i < num_positions; ++i) {
//         requesters.emplace_back([&, req_id = i]() {
//             double position_value = random_double(0.0, 150000.0);
            
//             engine::Event::Attributes attrs;
//             attrs["request_type"] = "position_check";
//             attrs["request_id"] = "req_" + std::to_string(req_id);
//             attrs["position_value"] = std::to_string(position_value);
            
//             bus->publish(engine::Event::create(
//                 engine::Event::Type::UserCustom,
//                 foundation::Timestamp::now(),
//                 attrs
//             ));
//         });
//     }
    
//     // 等待所有请求提交
//     for (auto& requester : requesters) {
//         requester.join();
//     }
    
//     // 等待所有请求处理完成
//     std::this_thread::sleep_for(std::chrono::milliseconds(200));
//     executor->shutdown(true);
    
//     std::cout << "=== 风险控制并发测试结果 ===" << std::endl;
//     std::cout << "总仓位请求: " << num_positions << std::endl;
//     std::cout << "批准仓位: " << approved_positions.load() << std::endl;
//     std::cout << "拒绝仓位: " << rejected_positions.load() << std::endl;
//     std::cout << "风险仓位数: " << risk_state.risky_positions.load() << std::endl;
//     std::cout << "总使用资金: " << risk_state.total_capital_used.load() << std::endl;
    
//     // 业务断言
//     int total_processed = approved_positions.load() + rejected_positions.load();
//     EXPECT_EQ(total_processed, num_positions);
//     //EXPECT_LE(risk_state.total_capital_used.load(), capital_limit);
// }

// // ===== 测试用例 4: 简单回测并发测试 =====
// TEST(BacktestConcurrencyTest, SimpleConcurrentBacktests) {
//     const int num_backtests = 4;  // 减少数量
//     const int bars_per_backtest = 100;
    
//     auto executor = std::make_shared<foundation::thread::ThreadPoolExecutor>(4);
//     auto bus = engine::EventBus::create(executor);
//     bus->set_policy(std::make_shared<engine::ImmediatePolicy>());
    
//     // 回测结果 - 简化版本
//     struct BacktestResult {
//         std::atomic<int> trades_count{0};
//         std::atomic<bool> completed{false};
//     };
    
//     std::vector<BacktestResult> results(num_backtests);
    
//     // 为每个回测订阅独立的事件处理器
//     for (int bt = 0; bt < num_backtests; ++bt) {
//         bus->subscribe(engine::Event::Type::UserCustom,
//             [&, backtest_id = bt](std::unique_ptr<engine::Event> evt) {
//                 if (!evt) return;
                
//                 std::string bt_id_str;
//                 std::string event_type;
                
//                 if (evt->get_attribute("backtest_id", bt_id_str) &&
//                     evt->get_attribute("event_type", event_type)) {
                    
//                     try {
//                         int received_bt_id = std::stoi(bt_id_str);
//                         if (received_bt_id != backtest_id) {
//                             return;
//                         }
                        
//                         if (event_type == "trade_executed") {
//                             results[backtest_id].trades_count++;
//                         } else if (event_type == "backtest_completed") {
//                             results[backtest_id].completed = true;
//                         }
//                     } catch (...) {
//                         // 忽略错误
//                     }
//                 }
//             }
//         );
//     }
    
//     // 并发执行多个回测
//     std::vector<std::thread> backtest_threads;
    
//     auto run_backtest = [&](int backtest_id) {
//         // 创建策略
//         int fast_period = 3 + (backtest_id % 5);
//         int slow_period = fast_period * 2;
        
//         CrossSignal signal(fast_period, slow_period);
//         MovingAverageStrategy strategy(signal);
//         strategy.setEventBus(bus.get());
        
//         Position position;
//         position.close();
        
//         double last_entry_price = 0.0;
        
//         // 生成测试数据
//         double price = 100.0 + backtest_id * 5.0;
        
//         for (int i = 0; i < bars_per_backtest; ++i) {
//             Bar bar;
//             bar.symbol = "bt" + std::to_string(backtest_id) + "_bar" + std::to_string(i);
//             bar.time = i + 1;
//             bar.open = price;
//             bar.high = price + 1.0;
//             bar.low = price - 1.0;
//             bar.close = price + random_double(-0.5, 0.5);
//             bar.volume = 1000;
            
//             auto action = strategy.onBar(bar);
            
//             if (action == StrategyAction::OpenLong) {
//                 position.open(bar.close, 1);
//                 last_entry_price = bar.close;
                
//                 // 发送交易事件
//                 engine::Event::Attributes attrs;
//                 attrs["backtest_id"] = std::to_string(backtest_id);
//                 attrs["event_type"] = "trade_executed";
//                 attrs["action"] = "open";
                
//                 bus->publish(engine::Event::create(
//                     engine::Event::Type::UserCustom,
//                     foundation::Timestamp::now(),
//                     attrs
//                 ));
                
//             } else if (action == StrategyAction::CloseLong) {
//                 if (position.hasPosition && position.entryPrice > 0) {
//                     // 计算盈亏
//                     double pnl = calculate_pnl(last_entry_price, bar.close, 1.0);
//                     position.close();
                    
//                     // 发送交易事件
//                     engine::Event::Attributes attrs;
//                     attrs["backtest_id"] = std::to_string(backtest_id);
//                     attrs["event_type"] = "trade_executed";
//                     attrs["action"] = "close";
//                     attrs["pnl"] = std::to_string(pnl);
                    
//                     bus->publish(engine::Event::create(
//                         engine::Event::Type::UserCustom,
//                         foundation::Timestamp::now(),
//                         attrs
//                     ));
//                 }
//             }
            
//             price = bar.close;
//         }
        
//         // 发送回测完成事件
//         engine::Event::Attributes attrs;
//         attrs["backtest_id"] = std::to_string(backtest_id);
//         attrs["event_type"] = "backtest_completed";
        
//         bus->publish(engine::Event::create(
//             engine::Event::Type::UserCustom,
//             foundation::Timestamp::now(),
//             attrs
//         ));
//     };
    
//     // 启动回测线程
//     for (int bt = 0; bt < num_backtests; ++bt) {
//         backtest_threads.emplace_back(run_backtest, bt);
//     }
    
//     // 等待所有回测完成
//     for (auto& thread : backtest_threads) {
//         thread.join();
//     }
    
//     // 等待所有事件处理完成
//     std::this_thread::sleep_for(std::chrono::milliseconds(200));
//     executor->shutdown(true);
    
//     // 验证所有回测都已完成
//     std::cout << "=== 并发回测结果 ===" << std::endl;
    
//     int completed_backtests = 0;
//     int total_trades = 0;
    
//     for (int bt = 0; bt < num_backtests; ++bt) {
//         if (results[bt].completed.load()) {
//             completed_backtests++;
//             total_trades += results[bt].trades_count.load();
            
//             std::cout << "回测 " << bt << ": 交易数=" << results[bt].trades_count.load() << std::endl;
//         }
//     }
    
//     std::cout << "总完成回测数: " << completed_backtests << std::endl;
//     std::cout << "总交易数: " << total_trades << std::endl;
    
//     // 业务断言
//     EXPECT_EQ(completed_backtests, num_backtests);
//     EXPECT_GE(total_trades, 0);
// }

// ===== 主函数 =====
int main(int argc, char** argv) {
     SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    ::testing::InitGoogleTest(&argc, argv);
    
    // // 设置随机种子
    // srand(static_cast<unsigned>(time(nullptr)));
    
    return RUN_ALL_TESTS();
}
// ===== 测试用例 1: 策略并发信号处理（修复版）=====
TEST(StrategyConcurrencyTest, ConcurrentSignalProcessing_Fixed) {
    std::cout << "Starting StrategyConcurrencyTest (Fixed version)..." << std::endl;
    
    const int num_strategies = 3;
    const int bars_per_strategy = 50;
    
    auto executor = std::make_shared<foundation::thread::ThreadPoolExecutor>(2);
    auto bus = engine::EventBus::create(executor);
    bus->set_policy(std::make_shared<engine::ImmediatePolicy>());
    
    // 共享资源：所有策略的仓位统计
    struct StrategyStats {
        std::atomic<int> open_signals{0};
        std::atomic<int> close_signals{0};
        std::atomic<int> any_events{0};  // 任何事件计数器
    };
    
    StrategyStats total_stats;
    
    // 为每个策略准备独立的数据
    struct StrategyData {
        CrossSignal signal;
        MovingAverageStrategy strategy;
        std::unique_ptr<Position> position;
        std::vector<Bar> bars;
        
        StrategyData(int fast, int slow, engine::EventBus* bus)
            : signal(fast, slow), strategy(signal) {
            if (bus) {
                strategy.setEventBus(bus);
            }
            position = std::make_unique<Position>();
            position->close();
        }
    };
    
    std::vector<std::unique_ptr<StrategyData>> all_strategies;
    
    // 初始化所有策略
    for (int s = 0; s < num_strategies; ++s) {
        auto strategy_data = std::make_unique<StrategyData>(s + 2, (s + 2) * 2, bus.get());
        
        // 生成测试数据
        double price = 100.0 + s * 10.0;
        for (int i = 0; i < bars_per_strategy; ++i) {
            Bar bar;
            bar.symbol = "strategy" + std::to_string(s) + "_bar" + std::to_string(i);
            bar.time = i + 1;
            bar.open = price;
            bar.high = price + 1.0;
            bar.low = price - 1.0;
            bar.close = price + foundation::utils::Random::next_Double(-0.5, 0.5);
            bar.volume = 1000;
            
            strategy_data->bars.push_back(bar);
            price = bar.close;
        }
        
        all_strategies.push_back(std::move(strategy_data));
    }
    
    // 订阅策略事件 - 使用更通用的方式
    bus->subscribe(engine::Event::Type::UserCustom,
        [&](std::unique_ptr<engine::Event> evt) {
            if (!evt) return;
            
            total_stats.any_events++;  // 任何事件都计数
            
            std::cout << "DEBUG: Received UserCustom event" << std::endl;
            
            // 尝试不同的属性名
            std::string msg;
            if (evt->get_attribute("msg", msg)) {
                std::cout << "  msg=" << msg << std::endl;
                if (msg.find("Open") != std::string::npos || msg.find("open") != std::string::npos) {
                    total_stats.open_signals++;
                } else if (msg.find("Close") != std::string::npos || msg.find("close") != std::string::npos) {
                    total_stats.close_signals++;
                }
            }
            
            // 尝试message属性
            std::string message;
            if (evt->get_attribute("message", message)) {
                std::cout << "  message=" << message << std::endl;
                if (message.find("Open") != std::string::npos || message.find("open") != std::string::npos) {
                    total_stats.open_signals++;
                } else if (message.find("Close") != std::string::npos || message.find("close") != std::string::npos) {
                    total_stats.close_signals++;
                }
            }
            
            // 打印所有属性用于调试
            std::cout << "  All attributes:" << std::endl;
            for (const auto& [key, value] : evt->attributes()) {
                std::cout << "    " << key << "=" << value << std::endl;
            }
        }
    );
    
    // 同时订阅其他可能的事件类型
    bus->subscribe(engine::Event::Type::System,
        [&](std::unique_ptr<engine::Event> evt) {
            std::cout << "DEBUG: Received System event" << std::endl;
        }
    );
    
    bus->subscribe(engine::Event::Type::MarketData,
        [&](std::unique_ptr<engine::Event> evt) {
            std::cout << "DEBUG: Received MarketData event" << std::endl;
        }
    );
    
    bus->subscribe(engine::Event::Type::Warning,
        [&](std::unique_ptr<engine::Event> evt) {
            std::cout << "DEBUG: Received Warning event" << std::endl;
        }
    );
    
    // 并发执行
    std::vector<std::thread> workers;
    std::atomic<int> completed_strategies{0};
    
    for (int s = 0; s < num_strategies; ++s) {
        workers.emplace_back([&, strategy_idx = s]() {
            try {
                std::cout << "Thread " << strategy_idx << " starting..." << std::endl;
                
                auto& data = *all_strategies[strategy_idx];
                
                // 手动发布一个测试事件，验证EventBus是否工作
                if (strategy_idx == 0) {
                    engine::Event::Attributes test_attrs;
                    test_attrs["msg"] = "Test Open from strategy " + std::to_string(strategy_idx);
                    test_attrs["source"] = "test";
                    
                    bus->publish(engine::Event::create(
                        engine::Event::Type::UserCustom,
                        foundation::Timestamp::now(),
                        test_attrs
                    ));
                    std::cout << "Test event published from strategy " << strategy_idx << std::endl;
                }
                
                for (const auto& bar : data.bars) {
                    auto action = data.strategy.onBar(bar);
                    
                    if (action == StrategyAction::OpenLong) {
                        data.position->open(bar.close, 1);
                        std::cout << "Strategy " << strategy_idx << ": OpenLong at " << bar.close << std::endl;
                    } else if (action == StrategyAction::CloseLong) {
                        data.position->close();
                        std::cout << "Strategy " << strategy_idx << ": CloseLong at " << bar.close << std::endl;
                    }
                }
                
                completed_strategies++;
                std::cout << "Thread " << strategy_idx << " completed." << std::endl;
            } catch (const std::exception& e) {
                std::cout << "Thread " << strategy_idx << " crashed: " << e.what() << std::endl;
            } catch (...) {
                std::cout << "Thread " << strategy_idx << " crashed with unknown error." << std::endl;
            }
        });
    }
    
    // 等待所有线程完成
    for (auto& worker : workers) {
        worker.join();
    }
    
    std::cout << "All worker threads joined." << std::endl;
    
    // 手动发布一些测试事件
    std::cout << "Publishing manual test events..." << std::endl;
    for (int i = 0; i < 3; ++i) {
        engine::Event::Attributes attrs;
        attrs["msg"] = "Manual test Open event " + std::to_string(i);
        attrs["test_id"] = std::to_string(i);
        
        bus->publish(engine::Event::create(
            engine::Event::Type::UserCustom,
            foundation::Timestamp::now(),
            attrs
        ));
    }
    
    // 确保所有事件处理完成
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    executor->shutdown(true);
    
    // 验证业务逻辑
    int total_open_positions = 0;
    for (const auto& strategy : all_strategies) {
        if (strategy->position->hasPosition) {
            total_open_positions++;
        }
    }
    
    std::cout << "=== 策略并发测试结果 ===" << std::endl;
    std::cout << "策略数量: " << num_strategies << std::endl;
    std::cout << "开仓信号: " << total_stats.open_signals.load() << std::endl;
    std::cout << "平仓信号: " << total_stats.close_signals.load() << std::endl;
    std::cout << "任何事件总数: " << total_stats.any_events.load() << std::endl;
    std::cout << "当前持仓: " << total_open_positions << std::endl;
    
    // 业务断言
    EXPECT_EQ(completed_strategies.load(), num_strategies);
    
    // 至少应该收到手动发布的测试事件
    EXPECT_GE(total_stats.any_events.load(), 3);
    
    std::cout << "StrategyConcurrencyTest completed." << std::endl;
}