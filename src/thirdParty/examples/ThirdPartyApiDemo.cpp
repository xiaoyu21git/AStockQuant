/**
 * @file ThirdPartyApiDemo.cpp
 * @brief 第三方API使用示例
 * 
 * 演示如何使用第三方API统一接口库调用掘金等平台接口。
 */

#include "../include/ThirdPartyApi.h"
#include <iostream>
#include <chrono>
#include <thread>

using namespace thirdparty;

// 市场数据回调函数
void on_market_data(const MarketData& data) {
    std::cout << "[市场数据] " << data.symbol 
              << " 收盘价: " << data.close
              << " 成交量: " << data.volume
              << " 时间: " << std::chrono::system_clock::to_time_t(data.timestamp)
              << std::endl;
}

// 订单回调函数
void on_order(const OrderInfo& order) {
    std::string side_str = (order.side == OrderSide::BUY) ? "买入" : "卖出";
    std::string status_str;
    
    switch (order.status) {
        case OrderStatus::PENDING: status_str = "待提交"; break;
        case OrderStatus::SUBMITTED: status_str = "已提交"; break;
        case OrderStatus::PARTIAL_FILLED: status_str = "部分成交"; break;
        case OrderStatus::FILLED: status_str = "全部成交"; break;
        case OrderStatus::CANCELLED: status_str = "已取消"; break;
        case OrderStatus::REJECTED: status_str = "已拒绝"; break;
        default: status_str = "未知";
    }
    
    std::cout << "[订单回调] " << order.order_id
              << " " << order.symbol
              << " " << side_str
              << " " << order.volume << "股"
              << " @ " << order.price
              << " 状态: " << status_str
              << std::endl;
}

// 持仓回调函数
void on_position(const PositionInfo& position) {
    std::cout << "[持仓回调] " << position.symbol
              << " 持仓: " << position.volume
              << " 成本: " << position.cost_price
              << " 市值: " << position.market_value
              << " 盈亏: " << position.float_profit
              << std::endl;
}

// 账户回调函数
void on_account(const AccountInfo& account) {
    std::cout << "[账户回调] " << account.account_id
              << " 总资产: " << account.total_asset
              << " 可用资金: " << account.available_cash
              << " 持仓市值: " << account.market_value
              << std::endl;
}

// 错误回调函数
void on_error(int error_code, const std::string& error_msg) {
    std::cerr << "[错误回调] 错误码: " << error_code
              << " 错误信息: " << error_msg
              << std::endl;
}

// 演示掘金API使用
void demo_jujin_api() {
    std::cout << "\n=== 掘金API演示 ===" << std::endl;
    
    try {
        // 创建掘金API实例
        auto api = ThirdPartyApiFactory::create_jujin_api();
        
        // 配置参数
        ConfigParams config;
        config.platform = PlatformType::JUJIN;
        config.token = "your_jujin_token_here";  // 替换为实际的掘金token
        config.account_id = "your_account_id_here";  // 替换为实际的账户ID
        config.server_url = "https://www.myquant.cn";
        
        // 额外参数
        config.extra_params["strategy_id"] = "demo_strategy";
        config.extra_params["mode"] = "2";  // 回测模式
        
        // 初始化API
        if (!api->initialize(config)) {
            std::cerr << "初始化API失败" << std::endl;
            return;
        }
        
        std::cout << "API初始化成功" << std::endl;
        
        // 设置回调函数
        api->set_order_callback(on_order);
        api->set_position_callback(on_position);
        api->set_account_callback(on_account);
        api->set_error_callback(on_error);
        
        // 连接平台
        if (!api->connect()) {
            std::cerr << "连接平台失败" << std::endl;
            return;
        }
        
        std::cout << "连接平台成功" << std::endl;
        
        // 等待连接稳定
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 获取账户信息
        std::cout << "\n获取账户信息..." << std::endl;
        AccountInfo account = api->get_account_info();
        std::cout << "账户ID: " << account.account_id << std::endl;
        std::cout << "总资产: " << account.total_asset << std::endl;
        std::cout << "可用资金: " << account.available_cash << std::endl;
        
        // 获取持仓信息
        std::cout << "\n获取持仓信息..." << std::endl;
        auto positions = api->get_positions();
        std::cout << "持仓数量: " << positions.size() << std::endl;
        for (const auto& pos : positions) {
            std::cout << "  " << pos.symbol << ": " << pos.volume << "股" << std::endl;
        }
        
        // 获取订单列表
        std::cout << "\n获取订单列表..." << std::endl;
        auto orders = api->get_orders();
        std::cout << "订单数量: " << orders.size() << std::endl;
        
        // 订阅行情
        std::cout << "\n订阅行情..." << std::endl;
        std::vector<std::string> symbols = {"000001.SZ", "600000.SH"};
        if (api->subscribe_market_data(symbols, MarketDataType::TICK, on_market_data)) {
            std::cout << "行情订阅成功" << std::endl;
        } else {
            std::cout << "行情订阅失败" << std::endl;
        }
        
        // 模拟交易（仅演示，不实际下单）
        std::cout << "\n模拟交易演示..." << std::endl;
        
        // 获取实时行情
        auto quotes = api->get_realtime_quotes(symbols);
        if (!quotes.empty()) {
            std::cout << "获取到 " << quotes.size() << " 个实时行情" << std::endl;
            for (const auto& quote : quotes) {
                std::cout << "  " << quote.symbol << ": " << quote.close << std::endl;
            }
            
            // 模拟下单（使用第一个标的）
            if (!quotes.empty()) {
                std::string symbol = quotes[0].symbol;
                double price = quotes[0].close;
                
                std::cout << "\n模拟下单: " << symbol 
                          << " 价格: " << price
                          << " 数量: 100" << std::endl;
                
                std::string order_id = api->place_order(
                    symbol,
                    OrderSide::BUY,
                    OrderType::LIMIT,
                    price,
                    100);
                
                if (!order_id.empty()) {
                    std::cout << "下单成功，订单ID: " << order_id << std::endl;
                    
                    // 等待一段时间
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    
                    // 模拟撤单
                    std::cout << "\n模拟撤单: " << order_id << std::endl;
                    if (api->cancel_order(order_id)) {
                        std::cout << "撤单成功" << std::endl;
                    } else {
                        std::cout << "撤单失败" << std::endl;
                    }
                } else {
                    std::cout << "下单失败" << std::endl;
                }
            }
        }
        
        // 获取历史数据
        std::cout << "\n获取历史数据..." << std::endl;
        auto end_time = std::chrono::system_clock::now();
        auto start_time = end_time - std::chrono::hours(24 * 7);  // 最近7天
        
        auto history_data = api->get_market_data(
            "000001.SZ",
            MarketDataType::BAR_1D,
            start_time,
            end_time);
        
        std::cout << "获取到 " << history_data.size() << " 条历史数据" << std::endl;
        if (!history_data.empty()) {
            std::cout << "最新数据: " << history_data.back().symbol
                      << " 收盘价: " << history_data.back().close
                      << " 时间: " << std::chrono::system_clock::to_time_t(history_data.back().timestamp)
                      << std::endl;
        }
        
        // 等待一段时间接收回调
        std::cout << "\n等待接收回调数据..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // 取消订阅
        std::cout << "\n取消订阅..." << std::endl;
        api->unsubscribe_market_data(symbols, MarketDataType::TICK);
        
        // 断开连接
        std::cout << "\n断开连接..." << std::endl;
        api->disconnect();
        
        std::cout << "演示完成" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "演示过程中发生错误: " << e.what() << std::endl;
    }
}

// 演示工厂方法使用
void demo_factory_method() {
    std::cout << "\n=== 工厂方法演示 ===" << std::endl;
    
    try {
        // 使用工厂方法创建不同平台的API
        std::cout << "1. 创建掘金API..." << std::endl;
        auto jujin_api = ThirdPartyApiFactory::create_api(PlatformType::JUJIN);
        std::cout << "   平台名称: " << jujin_api->get_platform_name() << std::endl;
        
        std::cout << "2. 直接创建掘金API..." << std::endl;
        auto jujin_api2 = ThirdPartyApiFactory::create_jujin_api();
        std::cout << "   平台名称: " << jujin_api2->get_platform_name() << std::endl;
        
        // 尝试创建其他平台API（会抛出异常）
        try {
            std::cout << "3. 尝试创建聚宽API..." << std::endl;
            auto juquan_api = ThirdPartyApiFactory::create_api(PlatformType::JUQUAN);
        } catch (const std::exception& e) {
            std::cout << "   预期中的错误: " << e.what() << std::endl;
        }
        
        std::cout << "工厂方法演示完成" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "工厂方法演示错误: " << e.what() << std::endl;
    }
}

// 演示工具函数使用
void demo_utility_functions() {
    std::cout << "\n=== 工具函数演示 ===" << std::endl;
    
    // 代码格式转换
    std::string internal_symbol = "000001.SZ";
    
    std::cout << "内部代码格式: " << internal_symbol << std::endl;
    
    // 转换为掘金格式
    std::string jujin_symbol = convert_symbol_format(internal_symbol, PlatformType::JUJIN);
    std::cout << "掘金代码格式: " << jujin_symbol << std::endl;
    
    // 转换回内部格式
    std::string internal_again = convert_to_internal_format(jujin_symbol, PlatformType::JUJIN);
    std::cout << "转换回内部格式: " << internal_again << std::endl;
    
    // 验证转换正确性
    if (internal_symbol == internal_again) {
        std::cout << "代码格式转换正确" << std::endl;
    } else {
        std::cout << "代码格式转换错误" << std::endl;
    }
    
    // 测试其他平台
    std::cout << "\n测试其他平台代码格式:" << std::endl;
    
    // 聚宽格式
    std::string juquan_symbol = convert_symbol_format(internal_symbol, PlatformType::JUQUAN);
    std::cout << "聚宽代码格式: " << juquan_symbol << std::endl;
    
    // 米筐格式（与内部格式相同）
    std::string ricequant_symbol = convert_symbol_format(internal_symbol, PlatformType::RICEQUANT);
    std::cout << "米筐代码格式: " << ricequant_symbol << std::endl;
    
    std::cout << "工具函数演示完成" << std::endl;
}

int main() {
    std::cout << "第三方API统一接口库演示程序" << std::endl;
    std::cout << "==========================" << std::endl;
    
    try {
        // 演示工具函数
        demo_utility_functions();
        
        // 演示工厂方法
        demo_factory_method();
        
        // 演示掘金API使用
        demo_jujin_api();
        
        std::cout << "\n所有演示完成" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "\n程序运行错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}