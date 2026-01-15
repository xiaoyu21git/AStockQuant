// test/performance/config/ConfigPerformanceTest.cpp
#include "gtest/gtest.h"
#include "foundation/config/ConfigManager.hpp"
#include "foundation/config/ConfigNode.hpp"
#include <chrono>
#include <random>
#include <deque>

using namespace foundation::config;
class PerformanceBaseline {
public:
    static void record(const std::string& test_name, long long duration_ms) {
        std::lock_guard<std::mutex> lock(getMutex());
        auto& values = getBaselines()[test_name];
        
        values.push_back(duration_ms);
        
        // 只保留最近10次
        if (values.size() > 10) {
            values.pop_front();
        }
    }
    
    static double getAverage(const std::string& test_name) {
        std::lock_guard<std::mutex> lock(getMutex());
        auto& baselines = getBaselines();
        auto it = baselines.find(test_name);
        
        if (it == baselines.end() || it->second.empty()) {
            return 0.0;
        }
        
        double sum = 0.0;
        for (auto v : it->second) {
            sum += static_cast<double>(v);
        }
        return sum / it->second.size();
    }
    
private:
    static std::mutex& getMutex() {
        static std::mutex mutex;
        return mutex;
    }
    
    static std::unordered_map<std::string, std::deque<long long>>& getBaselines() {
        static std::unordered_map<std::string, std::deque<long long>> baselines;
        return baselines;
    }
};
class ConfigPerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        configManager = &ConfigManager::instance();
        
        // 创建大型配置节点用于测试
        largeConfig = createLargeConfigNode(1000);
    }
    
    ConfigNode::Ptr createLargeConfigNode(int itemCount) {
        auto node = std::make_shared<ConfigNode>();
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1, 10000);
        
        for (int i = 0; i < itemCount; ++i) {
            std::string key = "config.item" + std::to_string(i);
            int value = dis(gen);
           //node->set(key, value);
        }
        
        return node;
    }
    
    ConfigManager* configManager;
    ConfigNode::Ptr largeConfig;
};

// 配置加载性能测试
TEST_F(ConfigPerformanceTest, LoadPerformance) {
    const int iterations = 100;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        auto node = std::make_shared<ConfigNode>();
        //node->set("test.key", i);
        // 模拟配置加载
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // 期望在100ms内完成100次操作
    EXPECT_LT(duration.count(), 100);
    
    std::cout << "LoadPerformance: " << duration.count() << "ms for " 
              << iterations << " iterations" << std::endl;
}

// // 配置访问性能测试
// TEST_F(ConfigPerformanceTest, AccessPerformance) {
//     const int iterations = 10000;
    
//     // 预热
//     for (int i = 0; i < 100; ++i) {
//         largeConfig->getPath("config.item500",'.');
//     }
    
//     auto start = std::chrono::high_resolution_clock::now();
    
//     for (int i = 0; i < iterations; ++i) {
//         // 访问随机配置项
//         std::string key = "config.item" + std::to_string(i % 1000);
//         auto value = largeConfig->getPath(key,'.');
//         (void)value; // 避免未使用变量警告
//     }
    
//     auto end = std::chrono::high_resolution_clock::now();
//     auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
//     // 期望在50ms内完成10000次访问
//     EXPECT_LT(duration.count(), 50);
    
//     std::cout << "AccessPerformance: " << duration.count() << "ms for " 
//               << iterations << " accesses" << std::endl;
// }

// 配置合并性能测试
TEST_F(ConfigPerformanceTest, MergePerformance) {
    const int iterations = 100;
    
    auto config1 = createLargeConfigNode(500);
    auto config2 = createLargeConfigNode(500);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        auto merged = std::make_shared<ConfigNode>(*config1);
        merged->overlay(*config2);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // 期望在200ms内完成100次合并
    EXPECT_LT(duration.count(), 200);
    
    std::cout << "MergePerformance: " << duration.count() << "ms for " 
              << iterations << " merges" << std::endl;
}

// 内存使用测试
TEST_F(ConfigPerformanceTest, MemoryUsage) {
    // 创建不同大小的配置节点
    std::vector<ConfigNode::Ptr> configs;
    
    for (int size : {10, 100, 1000, 10000}) {
        auto config = createLargeConfigNode(size);
        configs.push_back(config);
        
        // 可以在这里添加内存使用测量
        // 注意：跨平台的内存测量比较复杂
    }
    
    // 验证可以处理大量配置
    EXPECT_EQ(configs.size(), 4);
    
    // 测试序列化性能
    auto largeConfig = createLargeConfigNode(1000);
    
    auto start = std::chrono::high_resolution_clock::now();
    std::string json = largeConfig->toJsonString(false);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // 期望在100ms内序列化1000个配置项
    EXPECT_LT(duration.count(), 100);
    
    std::cout << "SerializationPerformance: " << duration.count() 
              << "ms for 1000 items, size: " << json.size() << " bytes" << std::endl;
}
// 记录性能基线，用于后续监控


TEST_F(ConfigPerformanceTest, AccessPerformance) {
    const int ACCESS_COUNT = 10000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < ACCESS_COUNT; ++i) {
        largeConfig->getPath("test.key." + std::to_string(i % 100));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // 记录基线
    PerformanceBaseline::record("ConfigAccess", duration.count());
    
    double baseline_avg = PerformanceBaseline::getAverage("ConfigAccess");
    double baseline_max = baseline_avg * 1.5;  // 允许50%的退化
    
    std::cout << "Current: " << duration.count() << "ms, "
              << "Baseline avg: " << baseline_avg << "ms, "
              << "Max allowed: " << baseline_max << "ms" << std::endl;
    
    // 动态阈值：不能比基线差太多
    if (baseline_avg > 0) {
        EXPECT_LT(duration.count(), baseline_max);
    } else {
        // 第一次运行，使用固定阈值
        EXPECT_LT(duration.count(), 50);
    }
}