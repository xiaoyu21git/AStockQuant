#include "market/repository/RepositoryFactory.hpp"
#include "foundation.h"
#include <iostream>
#include <cassert>

// 初始化Foundation
bool initFoundation() {
    try {
        // 简单配置
        foundation::Config config;
        config.profile = "test";
        config.enable_console_log = true;
        config.enable_file_log = false;
        config.log_level = foundation::LogLevel::DEBUG;
        
        return foundation::init();
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize Foundation: " << e.what() << std::endl;
        return false;
    }
}

void testRepositoryWithFoundation() {
    using namespace domain::market::repository;
    
    LOG_INFO("Starting repository tests with Foundation...");
    
    // 创建内存存储
    auto repo = RepositoryFactory::create(RepositoryType::MEMORY);
    assert(repo != nullptr);
    
    // 测试连接
    LOG_INFO("Testing connection...");
    assert(repo->connect());
    assert(repo->isConnected());
    
    // 创建测试数据
    LOG_INFO("Creating test data...");
    std::vector<domain::model::Bar> bars = {
        {"AAPL", 1700000000000, 150.0, 152.0, 149.5, 151.5, 1000000.0},
        {"AAPL", 1700000060000, 151.5, 153.0, 151.0, 152.5, 1200000.0},
        {"AAPL", 1700000120000, 152.5, 154.0, 152.0, 153.5, 1100000.0},
        {"GOOGL", 1700000000000, 2800.0, 2820.0, 2790.0, 2810.0, 500000.0},
        {"GOOGL", 1700000060000, 2810.0, 2830.0, 2805.0, 2825.0, 600000.0}
    };
    
    // 测试保存
    LOG_INFO("Testing save operations...");
    assert(repo->saveBars("AAPL", {bars[0], bars[1], bars[2]}));
    assert(repo->saveBars("GOOGL", {bars[3], bars[4]}));
    
    // 测试加载
    LOG_INFO("Testing load operations...");
    auto aapl_bars = repo->loadBars("AAPL");
    LOG_INFO("Loaded {} AAPL bars", aapl_bars.size());
    assert(aapl_bars.size() == 3);
    
    auto googl_bars = repo->loadBars("GOOGL");
    LOG_INFO("Loaded {} GOOGL bars", googl_bars.size());
    assert(googl_bars.size() == 2);
    
    // 测试范围查询
    LOG_INFO("Testing range queries...");
    auto filtered = repo->loadBars("AAPL", 1700000060000, 1700000120000);
    LOG_INFO("Filtered query returned {} bars", filtered.size());
    assert(filtered.size() == 2);
    
    // 测试最近数据
    LOG_INFO("Testing recent data queries...");
    auto recent = repo->loadRecentBars("AAPL", 2);
    LOG_INFO("Recent query returned {} bars", recent.size());
    assert(recent.size() == 2);
    assert(recent[0].time == 1700000120000); // 最新的
    
    // 测试存在性检查
    LOG_INFO("Testing existence checks...");
    assert(repo->barExists("AAPL", 1700000000000));
    assert(!repo->barExists("AAPL", 9999999999999));
    
    // 测试批量操作
    LOG_INFO("Testing batch operations...");
    std::map<std::string, std::vector<domain::model::Bar>> batch_data = {
        {"MSFT", {{"MSFT", 1700000000000, 350.0, 355.0, 348.0, 352.0, 800000.0}}},
        {"TSLA", {{"TSLA", 1700000000000, 250.0, 255.0, 248.0, 252.0, 900000.0}}}
    };
    
    repo->batchSaveBars(batch_data);
    assert(repo->loadBars("MSFT").size() == 1);
    assert(repo->loadBars("TSLA").size() == 1);
    
    // 测试删除
    LOG_INFO("Testing delete operations...");
    size_t deleted = repo->deleteBars("AAPL", 1700000000000, 1700000000000);
    LOG_INFO("Deleted {} AAPL bars", deleted);
    assert(deleted == 1);
    assert(repo->loadBars("AAPL").size() == 2);
    
    // 测试断开连接
    LOG_INFO("Testing disconnect...");
    repo->disconnect();
    assert(!repo->isConnected());
    
    LOG_INFO("All repository tests passed!");
}

int main() {
    std::cout << "=== Repository Test with Foundation ===" << std::endl;
    
    // 初始化Foundation
    if (!initFoundation()) {
        std::cerr << "Failed to initialize Foundation" << std::endl;
        return 1;
    }
    
    try {
        testRepositoryWithFoundation();
        std::cout << "\n✅ All tests passed successfully!" << std::endl;
        
        // 关闭Foundation
        foundation::shutdown();
        return 0;
    } catch (const std::exception& e) {
        LOG_ERROR("Test failed: {}", e.what());
        std::cerr << "\n❌ Test failed: " << e.what() << std::endl;
        
        foundation::shutdown();
        return 1;
    }
}