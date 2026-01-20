// examples/market_demo.cpp
#include "market/core/MarketData.h"
#include <iostream>
#include <chrono>
#include <thread>

int main() {
    // 获取管理器实例
    auto& manager = astock::market::MarketDataManager::instance();
    
    // 初始化模拟数据源
    if (!manager.initialize(
            astock::market::DataProviderFactory::ProviderType::SIMULATED,
            "update_interval_ms=1000;base_price=100.0")) {
        std::cerr << "Failed to initialize market data manager" << std::endl;
        return 1;
    }
    
    std::cout << "Market Data Manager initialized successfully" << std::endl;
    
    // 订阅回调
    auto callback_id = manager.register_kline_callback(
        [](const astock::market::KLine& kline) {
            std::cout << "KLine received: " << kline.to_string() << std::endl;
        });
    
    // 订阅股票数据
    manager.subscribe_kline(100001, 60);  // 1分钟K线
    manager.subscribe_kline(100002, 300); // 5分钟K线
    
    std::cout << "Subscribed to market data. Waiting for data..." << std::endl;
    
    // 获取历史数据
    auto now = std::chrono::system_clock::now();
    auto start_time = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count() - 3600; // 1小时前
    
    auto klines = manager.get_history_klines(
        100001, 60, start_time, start_time + 3600, 100);
    
    std::cout << "Retrieved " << klines.size() << " historical KLines" << std::endl;
    
    // 实时获取最新数据
    for (int i = 0; i < 10; ++i) {
        auto latest = manager.get_latest_kline(100001, 60);
        if (latest) {
            std::cout << "Latest KLine: " << latest->close 
                      << " @ " << latest->timestamp << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    // 清理
    manager.unsubscribe_kline(100001, 60);
    manager.unsubscribe_kline(100002, 300);
    manager.unregister_callback(callback_id);
    
    return 0;
}