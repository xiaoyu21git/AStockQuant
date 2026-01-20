// tests/test_market.cpp
#include <gtest/gtest.h>
#include "market/core/MarketData.h"
#include "market/providers/SimProvider.h"

TEST(MarketDataTest, SingletonInstance) {
    auto& manager1 = astock::market::MarketDataManager::instance();
    auto& manager2 = astock::market::MarketDataManager::instance();
    
    EXPECT_EQ(&manager1, &manager2);
}

TEST(MarketDataTest, InitializeWithSimProvider) {
    auto& manager = astock::market::MarketDataManager::instance();
    
    bool success = manager.initialize(
        astock::market::DataProviderFactory::ProviderType::SIMULATED,
        "update_interval_ms=100"
    );
    
    EXPECT_TRUE(success);
}

TEST(MarketDataTest, SubscribeAndGetData) {
    auto& manager = astock::market::MarketDataManager::instance();
    
    // 先初始化
    manager.initialize(
        astock::market::DataProviderFactory::ProviderType::SIMULATED,
        "update_interval_ms=100"
    );
    
    // 订阅数据
    bool subscribed = manager.subscribe_kline(100001, 60);
    EXPECT_TRUE(subscribed);
    
    // 获取历史数据
    auto klines = manager.get_history_klines(
        100001, 60, 
        1700000000,  // 开始时间
        1700003600,  // 1小时后
        10           // 限制10条
    );
    
    EXPECT_FALSE(klines.empty());
    EXPECT_LE(klines.size(), 10);
    
    // 检查数据有效性
    for (const auto& kline : klines) {
        EXPECT_TRUE(kline.is_valid());
        EXPECT_EQ(kline.symbol_id, 100001);
        EXPECT_EQ(kline.period, 60);
    }
}

TEST(SimProviderTest, GenerateData) {
    astock::market::SimProvider provider("update_interval_ms=10");
    
    EXPECT_TRUE(provider.connect());
    EXPECT_EQ(provider.get_status(), astock::market::ProviderStatus::CONNECTED);
    
    // 测试历史数据获取
    auto klines = provider.get_history_klines(
        100001, 60, 1700000000, 1700003600, 5);
    
    EXPECT_EQ(klines.size(), 5);
    
    provider.disconnect();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}