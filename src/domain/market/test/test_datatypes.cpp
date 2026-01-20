// tests/test_datatypes.cpp
#include <gtest/gtest.h>
#include "market/core/DataTypes.h"

using namespace astock::market;

TEST(DataTypeTest, KLineBasicConstruction) {
    KLine kline;
    
    // 测试默认值
    EXPECT_EQ(kline.symbol_id, 0);
    EXPECT_EQ(kline.timestamp, 0);
    EXPECT_DOUBLE_EQ(kline.open, 0.0);
    EXPECT_DOUBLE_EQ(kline.close, 0.0);
    EXPECT_DOUBLE_EQ(kline.high, 0.0);
    EXPECT_DOUBLE_EQ(kline.low, 0.0);
    EXPECT_DOUBLE_EQ(kline.volume, 0.0);
    EXPECT_DOUBLE_EQ(kline.amount, 0.0);
    EXPECT_DOUBLE_EQ(kline.turnover, 0.0);
}

TEST(DataTypeTest, KLineChangeRate) {
    KLine kline;
    
    // 测试涨跌幅计算
    kline.open = 100.0;
    kline.close = 105.0;
    EXPECT_DOUBLE_EQ(kline.change_rate(), 0.05);
    
    kline.open = 100.0;
    kline.close = 95.0;
    EXPECT_DOUBLE_EQ(kline.change_rate(), -0.05);
    
    // 测试除零情况
    kline.open = 0.0;
    kline.close = 100.0;
    EXPECT_DOUBLE_EQ(kline.change_rate(), 0.0);
}

TEST(DataTypeTest, KLineIsYang) {
    KLine kline;
    
    // 测试是否为阳线
    kline.open = 100.0;
    kline.close = 105.0;
    EXPECT_TRUE(kline.is_yang());
    
    kline.open = 100.0;
    kline.close = 95.0;
    EXPECT_FALSE(kline.is_yang());
    
    kline.open = 100.0;
    kline.close = 100.0;
    EXPECT_FALSE(kline.is_yang());
}

TEST(DataTypeTest, KLineAmplitude) {
    KLine kline;
    
    // 测试振幅计算
    kline.open = 100.0;
    kline.high = 105.0;
    kline.low = 98.0;
    EXPECT_DOUBLE_EQ(kline.amplitude(), 0.07);
    
    // 测试除零情况
    kline.open = 0.0;
    EXPECT_DOUBLE_EQ(kline.amplitude(), 0.0);
}

TEST(DataTypeTest, KLineIsValid) {
    KLine kline;
    
    // 无效数据
    kline.timestamp = 0;
    kline.open = 100.0;
    EXPECT_FALSE(kline.is_valid());
    
    // 有效数据
    kline.timestamp = 1700000000;
    kline.open = 100.0;
    kline.high = 102.0;
    kline.low = 98.0;
    kline.close = 101.0;
    EXPECT_TRUE(kline.is_valid());
    
    // 测试价格关系
    kline.high = 98.0;  // high < low
    kline.low = 102.0;
    EXPECT_FALSE(kline.is_valid());
}

TEST(DataTypeTest, KLineToString) {
    KLine kline;
    kline.symbol_id = 100001;
    kline.period = 60;
    kline.timestamp = 1700000000;
    kline.open = 100.0;
    kline.high = 102.0;
    kline.low = 98.5;
    kline.close = 101.5;
    kline.volume = 1500000;
    kline.amount = 152250000;
    kline.turnover = 1.5;
    
    std::string str = kline.to_string();
    EXPECT_TRUE(str.find("KLine") != std::string::npos);
    EXPECT_TRUE(str.find("symbol_id:100001") != std::string::npos);
    EXPECT_TRUE(str.find("O:100.0000") != std::string::npos);
}

TEST(DataTypeTest, TickDataBasic) {
    TickData tick;
    
    EXPECT_EQ(tick.symbol_id, 0);
    EXPECT_EQ(tick.timestamp, 0);
    EXPECT_EQ(tick.sequence, 0);
    EXPECT_DOUBLE_EQ(tick.price, 0.0);
    EXPECT_DOUBLE_EQ(tick.volume, 0.0);
    EXPECT_DOUBLE_EQ(tick.amount, 0.0);
    EXPECT_EQ(tick.direction, 0);
}

TEST(DataTypeTest, TickDataSpread) {
    TickData tick;
    
    // 测试买卖价差
    tick.bid_prices[0] = 100.0;
    tick.ask_prices[0] = 100.1;
    EXPECT_DOUBLE_EQ(tick.spread(), 0.1);
    
    // 测试零值
    tick.bid_prices[0] = 0.0;
    tick.ask_prices[0] = 0.0;
    EXPECT_DOUBLE_EQ(tick.spread(), 0.0);
}

TEST(DataTypeTest, TickDataMidPrice) {
    TickData tick;
    
    tick.bid_prices[0] = 100.0;
    tick.ask_prices[0] = 100.2;
    EXPECT_DOUBLE_EQ(tick.mid_price(), 100.1);
    
    // 当买卖价都为0时，返回最新价
    tick.bid_prices[0] = 0.0;
    tick.ask_prices[0] = 0.0;
    tick.price = 99.5;
    EXPECT_DOUBLE_EQ(tick.mid_price(), 99.5);
    
    // 当买卖价都为0且最新价为0时
    tick.price = 0.0;
    EXPECT_DOUBLE_EQ(tick.mid_price(), 0.0);
}

TEST(DataTypeTest, TickDataDirection) {
    TickData tick;
    
    tick.direction = 1;
    EXPECT_TRUE(tick.is_buy());
    EXPECT_FALSE(tick.is_sell());
    
    tick.direction = -1;
    EXPECT_FALSE(tick.is_buy());
    EXPECT_TRUE(tick.is_sell());
    
    tick.direction = 0;
    EXPECT_FALSE(tick.is_buy());
    EXPECT_FALSE(tick.is_sell());
}

TEST(DataTypeTest, TickDataIsValid) {
    TickData tick;
    
    // 无效数据
    EXPECT_FALSE(tick.is_valid());
    
    // 有效数据
    tick.timestamp = 1700000000000;
    tick.price = 100.0;
    tick.volume = 1000.0;
    EXPECT_TRUE(tick.is_valid());
    
    // 负成交量无效
    tick.volume = -100.0;
    EXPECT_FALSE(tick.is_valid());
}

TEST(DataTypeTest, DepthDataCalculations) {
    DepthData depth;
    depth.symbol_id = 100001;
    depth.timestamp = 1700000000;
    
    // 设置买卖五档
    for (int i = 0; i < 5; ++i) {
        depth.bid_prices.push_back(100.0 - i * 0.1);
        depth.bid_volumes.push_back(1000.0 + i * 100);
        depth.ask_prices.push_back(100.1 + i * 0.1);
        depth.ask_volumes.push_back(800.0 + i * 80);
    }
    
    // 测试总买卖量
    EXPECT_DOUBLE_EQ(depth.total_bid_volume(), 1000 + 1100 + 1200 + 1300 + 1400);
    EXPECT_DOUBLE_EQ(depth.total_ask_volume(), 800 + 880 + 960 + 1040 + 1120);
    
    // 测试不平衡度
    double bid_total = depth.total_bid_volume();
    double ask_total = depth.total_ask_volume();
    double expected_imbalance = (bid_total - ask_total) / (bid_total + ask_total);
    EXPECT_DOUBLE_EQ(depth.imbalance(), expected_imbalance);
}

TEST(DataTypeTest, DepthDataIsValid) {
    DepthData depth;
    
    // 空数据无效
    EXPECT_FALSE(depth.is_valid());
    
    // 时间戳为0无效
    depth.timestamp = 0;
    EXPECT_FALSE(depth.is_valid());
    
    // 买卖盘数量不一致无效
    depth.timestamp = 1700000000;
    depth.bid_prices.push_back(100.0);
    depth.bid_volumes.push_back(1000.0);
    depth.ask_prices.push_back(100.1);
    // 故意不添加ask_volumes
    EXPECT_FALSE(depth.is_valid());
    
    // 有效数据
    depth.ask_volumes.push_back(800.0);
    EXPECT_TRUE(depth.is_valid());
}

TEST(DataTypeTest, KLineBatchBasic) {
    KLineBatch batch;
    
    // 测试初始状态
    EXPECT_EQ(batch.size(), 0);
    EXPECT_TRUE(batch.empty());
    
    // 添加数据
    KLine kline;
    kline.symbol_id = 100001;
    kline.close = 100.0;
    
    batch.push_back(kline);
    EXPECT_EQ(batch.size(), 1);
    EXPECT_FALSE(batch.empty());
    
    // 访问数据
    EXPECT_EQ(batch[0].symbol_id, 100001);
    EXPECT_DOUBLE_EQ(batch[0].close, 100.0);
}

TEST(DataTypeTest, KLineBatchResizing) {
    // 测试容量为1的批次
    KLineBatch batch(1);
    
    KLine kline1;
    kline1.symbol_id = 100001;
    batch.push_back(kline1);
    
    KLine kline2;
    kline2.symbol_id = 100002;
    batch.push_back(kline2);  // 这会触发扩容
    
    EXPECT_GE(batch.size(), 2);
    EXPECT_EQ(batch[0].symbol_id, 100001);
    EXPECT_EQ(batch[1].symbol_id, 100002);
}

TEST(DataTypeTest, KLineBatchClear) {
    KLineBatch batch;
    
    KLine kline;
    kline.symbol_id = 100001;
    batch.push_back(kline);
    batch.push_back(kline);
    
    EXPECT_EQ(batch.size(), 2);
    
    batch.clear();
    EXPECT_EQ(batch.size(), 0);
    EXPECT_TRUE(batch.empty());
}

TEST(DataTypeTest, KLineBatchIteration) {
    KLineBatch batch(3);
    
    for (int i = 0; i < 3; ++i) {
        KLine kline;
        kline.symbol_id = 100000 + i;
        kline.close = 100.0 + i;
        batch.push_back(kline);
    }
    
    // 测试迭代器
    int count = 0;
    for (const auto& kline : batch) {
        EXPECT_EQ(kline.symbol_id, 100000 + count);
        EXPECT_DOUBLE_EQ(kline.close, 100.0 + count);
        count++;
    }
    EXPECT_EQ(count, 3);
}

TEST(DataTypeTest, MemoryAlignment) {
    // 测试KLine的内存对齐
    EXPECT_EQ(alignof(KLine), 64);
    
    // 测试结构体大小
    EXPECT_EQ(sizeof(KLine), 64);  // 根据我们的设计，应该是64字节
    
    // 测试TickData的大小
    EXPECT_EQ(sizeof(TickData), 
              sizeof(uint64_t) * 2 +   // timestamp, sequence
              sizeof(uint32_t) +       // symbol_id
              sizeof(double) * 3 +     // price, volume, amount
              sizeof(int32_t) +        // direction
              sizeof(double) * 20);    // 5档买卖价格和数量 (5*4)
    
    // 验证padding（可选）
    std::cout << "KLine size: " << sizeof(KLine) << " bytes" << std::endl;
    std::cout << "TickData size: " << sizeof(TickData) << " bytes" << std::endl;
}

TEST(DataTypeTest, PerformanceKLineCreation) {
    const int N = 1000000;
    std::vector<KLine> klines;
    klines.reserve(N);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < N; ++i) {
        KLine kline;
        kline.symbol_id = 100001 + i % 100;
        kline.timestamp = 1700000000 + i * 60;
        kline.open = 100.0 + i * 0.01;
        kline.high = kline.open + 0.5;
        kline.low = kline.open - 0.3;
        kline.close = kline.open + 0.2;
        kline.volume = 1000000 + i * 1000;
        kline.amount = kline.close * kline.volume;
        klines.push_back(kline);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Created " << N << " KLines in " << duration.count() << " microseconds" << std::endl;
    std::cout << "Average time per KLine: " << static_cast<double>(duration.count()) / N << " microseconds" << std::endl;
    
    // 验证数据
    EXPECT_EQ(klines.size(), N);
    EXPECT_TRUE(klines[0].is_valid());
}

TEST(DataTypeTest, PerformanceBatchOperations) {
    const int N = 100000;
    KLineBatch batch(N);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < N; ++i) {
        KLine kline;
        kline.symbol_id = 100001;
        kline.timestamp = 1700000000 + i * 60;
        kline.close = 100.0 + i * 0.01;
        batch.push_back(kline);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Batch push " << N << " KLines in " << duration.count() << " microseconds" << std::endl;
    
    // 测试批量计算
    start = std::chrono::high_resolution_clock::now();
    
    double total_change = 0.0;
    int yang_count = 0;
    
    for (size_t i = 0; i < batch.size(); ++i) {
        total_change += batch[i].change_rate();
        if (batch[i].is_yang()) {
            yang_count++;
        }
    }
    
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Processed " << batch.size() << " KLines in " << duration.count() << " microseconds" << std::endl;
    std::cout << "Yang lines: " << yang_count << "/" << batch.size() << std::endl;
    
    EXPECT_EQ(batch.size(), N);
}

// 测试边界条件
TEST(DataTypeTest, BoundaryConditions) {
    // 测试极端价格值
    KLine kline;
    kline.open = std::numeric_limits<double>::min();
    kline.close = std::numeric_limits<double>::max();
    
    // 这可能会导致溢出，但函数应该能处理
    double rate = kline.change_rate();
    EXPECT_TRUE(std::isfinite(rate) || std::isinf(rate));
    
    // 测试NaN值
    kline.open = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(std::isfinite(kline.change_rate()));
}

// 测试拷贝和移动语义
TEST(DataTypeTest, CopyAndMove) {
    KLine kline1;
    kline1.symbol_id = 100001;
    kline1.open = 100.0;
    kline1.close = 101.0;
    
    // 拷贝构造
    KLine kline2 = kline1;
    EXPECT_EQ(kline2.symbol_id, kline1.symbol_id);
    EXPECT_DOUBLE_EQ(kline2.open, kline1.open);
    
    // 修改原对象，拷贝不应受影响
    kline1.open = 200.0;
    EXPECT_DOUBLE_EQ(kline2.open, 100.0);
    
    // 移动构造
    KLine kline3 = std::move(kline1);
    EXPECT_EQ(kline3.symbol_id, 100001);
    EXPECT_DOUBLE_EQ(kline3.open, 200.0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // 设置详细输出
    testing::GTEST_FLAG(color) = "yes";
    
    return RUN_ALL_TESTS();
}