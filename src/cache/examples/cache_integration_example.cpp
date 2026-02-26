// cache_integration_example.cpp
// 缓存系统集成示例

#include "cache_facade.h"
#include "serialization.h"
#include <iostream>
#include <chrono>
#include <vector>

using namespace AStockQuantEngine::Cache;

// 示例：缓存股票日线数据
void example_stock_daily_cache() {
    std::cout << "=== 股票日线数据缓存示例 ===" << std::endl;
    
    // 1. 初始化缓存系统
    CacheConfig config;
    config.enabled = true;
    config.defaultTtl = std::chrono::seconds(300);
    
    config.localCache.enabled = true;
    config.localCache.maxSize = 10000;
    config.localCache.expireAfterAccess = std::chrono::seconds(3600);
    config.localCache.expireAfterWrite = std::chrono::seconds(300);
    
    config.redisCache.enabled = true;
    config.redisCache.host = "127.0.0.1";
    config.redisCache.port = 6379;
    
    // 设置股票数据缓存策略
    CachePolicy stockPolicy;
    stockPolicy.ttl = std::chrono::seconds(3600);  // 1小时
    stockPolicy.useLocalCache = true;
    stockPolicy.useRedisCache = true;
    stockPolicy.cacheEmptyResults = false;
    stockPolicy.maxSize = 10000;
    
    config.policies["stock:basic:"] = stockPolicy;
    config.policies["market:daily:"] = stockPolicy;
    
    auto& cache = CacheFacade::getInstance();
    if (!cache.initialize(config)) {
        std::cerr << "Failed to initialize cache system" << std::endl;
        return;
    }
    
    // 2. 模拟股票日线数据
    KLine klineData;
    klineData.symbol = "000001.SZ";
    klineData.trade_date = "2024-01-15";
    klineData.open = 10.5;
    klineData.high = 11.2;
    klineData.low = 10.3;
    klineData.close = 10.8;
    klineData.volume = 1000000;
    klineData.change_pct = 2.86;
    
    // 3. 生成缓存key
    std::string cacheKey = KeyGenerator::marketDaily(
        klineData.symbol, 
        klineData.trade_date, 
        klineData.trade_date
    );
    
    std::cout << "Cache key: " << cacheKey << std::endl;
    
    // 4. 设置缓存
    auto start = std::chrono::high_resolution_clock::now();
    cache.set(cacheKey, klineData, std::chrono::seconds(3600));
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Cache set time: " << duration.count() << " microseconds" << std::endl;
    
    // 5. 获取缓存
    KLine cachedKline;
    start = std::chrono::high_resolution_clock::now();
    bool found = cache.get(cacheKey, cachedKline);
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    if (found) {
        std::cout << "Cache hit! Retrieved from cache in " << duration.count() << " microseconds" << std::endl;
        std::cout << "Cached data: " << cachedKline.symbol 
                  << " " << cachedKline.trade_date 
                  << " Close: " << cachedKline.close << std::endl;
    } else {
        std::cout << "Cache miss" << std::endl;
    }
    
    // 6. 批量操作示例
    std::cout << "\n=== 批量操作示例 ===" << std::endl;
    
    std::vector<std::string> symbols = {"000001.SZ", "000002.SZ", "000003.SZ"};
    std::map<std::string, KLine> batchData;
    
    for (const auto& symbol : symbols) {
        KLine kline;
        kline.symbol = symbol;
        kline.trade_date = "2024-01-15";
        kline.close = 10.0 + (rand() % 100) / 10.0;
        
        std::string key = KeyGenerator::marketDaily(symbol, "2024-01-15", "2024-01-15");
        batchData[key] = kline;
    }
    
    // 批量设置
    cache.setBulk(batchData, std::chrono::seconds(1800));
    std::cout << "Batch set " << batchData.size() << " items" << std::endl;
    
    // 批量获取
    std::vector<std::string> keysToGet;
    for (const auto& [key, _] : batchData) {
        keysToGet.push_back(key);
    }
    
    auto batchResult = cache.getBulk<KLine>(keysToGet);
    std::cout << "Batch get retrieved " << batchResult.size() << " items" << std::endl;
    
    // 7. 统计信息
    std::cout << "\n=== 缓存统计信息 ===" << std::endl;
    CacheStats stats = cache.getStats();
    std::cout << "Hits: " << stats.hits << std::endl;
    std::cout << "Misses: " << stats.misses << std::endl;
    std::cout << "Hit rate: " << (stats.hitRate() * 100) << "%" << std::endl;
    std::cout << "Average get latency: " << stats.avgGetLatencyMs() << " ms" << std::endl;
    
    // 8. 清理
    cache.shutdown();
}

// 示例：集成到现有数据查询流程
void example_integration_with_data_service() {
    std::cout << "\n=== 与DataService集成示例 ===" << std::endl;
    
    // 模拟现有的DataService查询函数
    auto queryStockDataFromDatabase = [](const std::string& symbol, 
                                         const std::string& startDate,
                                         const std::string& endDate) -> std::vector<KLine> {
        // 模拟数据库查询
        std::vector<KLine> result;
        
        // 这里应该是实际的数据库查询逻辑
        // 为了示例，我们返回模拟数据
        for (int i = 0; i < 10; i++) {
            KLine kline;
            kline.symbol = symbol;
            kline.trade_date = "2024-01-" + std::to_string(10 + i);
            kline.open = 10.0 + i * 0.1;
            kline.high = 10.5 + i * 0.1;
            kline.low = 9.5 + i * 0.1;
            kline.close = 10.2 + i * 0.1;
            kline.volume = 1000000 + i * 100000;
            kline.change_pct = i * 0.5;
            result.push_back(kline);
        }
        
        std::cout << "Query from database: " << symbol 
                  << " (" << startDate << " to " << endDate << ")" 
                  << " - Found " << result.size() << " records" << std::endl;
        
        return result;
    };
    
    // 带缓存的查询函数
    auto queryStockDataWithCache = [&](const std::string& symbol,
                                       const std::string& startDate,
                                       const std::string& endDate) -> std::vector<KLine> {
        
        auto& cache = CacheFacade::getInstance();
        
        // 生成缓存key
        std::string cacheKey = KeyGenerator::marketDaily(symbol, startDate, endDate);
        
        // 尝试从缓存获取
        std::vector<KLine> cachedResult;
        if (cache.get(cacheKey, cachedResult)) {
            std::cout << "Cache hit for " << cacheKey << std::endl;
            return cachedResult;
        }
        
        std::cout << "Cache miss for " << cacheKey << ", querying database..." << std::endl;
        
        // 缓存未命中，查询数据库
        auto result = queryStockDataFromDatabase(symbol, startDate, endDate);
        
        // 将结果存入缓存
        if (!result.empty()) {
            cache.set(cacheKey, result, std::chrono::seconds(3600));
            std::cout << "Cached " << result.size() << " records for " << cacheKey << std::endl;
        }
        
        return result;
    };
    
    // 测试带缓存的查询
    std::cout << "\nFirst query (should miss cache):" << std::endl;
    auto result1 = queryStockDataWithCache("000001.SZ", "2024-01-10", "2024-01-20");
    
    std::cout << "\nSecond query (should hit cache):" << std::endl;
    auto result2 = queryStockDataWithCache("000001.SZ", "2024-01-10", "2024-01-20");
    
    std::cout << "\nFirst query result size: " << result1.size() << std::endl;
    std::cout << "Second query result size: " << result2.size() << std::endl;
    
    // 验证缓存一致性
    if (result1.size() == result2.size()) {
        std::cout << "Cache consistency check: PASSED" << std::endl;
    } else {
        std::cout << "Cache consistency check: FAILED" << std::endl;
    }
}

// 示例：缓存策略管理
void example_cache_policy_management() {
    std::cout << "\n=== 缓存策略管理示例 ===" << std::endl;
    
    auto& cache = CacheFacade::getInstance();
    
    // 设置不同数据类型的缓存策略
    CachePolicy realtimePolicy;
    realtimePolicy.ttl = std::chrono::seconds(5);  // 实时数据5秒过期
    realtimePolicy.useLocalCache = false;          // 实时数据不缓存到本地
    realtimePolicy.useRedisCache = true;
    realtimePolicy.cacheEmptyResults = false;
    
    CachePolicy historicalPolicy;
    historicalPolicy.ttl = std::chrono::seconds(86400);  // 历史数据24小时
    historicalPolicy.useLocalCache = true;
    historicalPolicy.useRedisCache = true;
    historicalPolicy.cacheEmptyResults = true;
    
    CachePolicy sessionPolicy;
    sessionPolicy.ttl = std::chrono::seconds(1800);  // 会话数据30分钟
    sessionPolicy.useLocalCache = false;
    sessionPolicy.useRedisCache = true;
    sessionPolicy.cacheEmptyResults = false;
    
    // 应用策略
    cache.setPolicy("market:realtime:", realtimePolicy);
    cache.setPolicy("market:daily:", historicalPolicy);
    cache.setPolicy("session:", sessionPolicy);
    
    // 测试策略应用
    std::string realtimeKey = KeyGenerator::marketRealtime("000001.SZ");
    std::string dailyKey = KeyGenerator::marketDaily("000001.SZ", "2024-01-15", "2024-01-15");
    std::string sessionKey = KeyGenerator::userSession("session123");
    
    auto realtimePolicyCheck = cache.getPolicy(realtimeKey);
    auto dailyPolicyCheck = cache.getPolicy(dailyKey);
    auto sessionPolicyCheck = cache.getPolicy(sessionKey);
    
    std::cout << "Realtime policy TTL: " << realtimePolicyCheck.ttl.count() << " seconds" << std::endl;
    std::cout << "Historical policy TTL: " << dailyPolicyCheck.ttl.count() << " seconds" << std::endl;
    std::cout << "Session policy TTL: " << sessionPolicyCheck.ttl.count() << " seconds" << std::endl;
}

int main() {
    std::cout << "AStockQuantEngine 缓存系统集成示例" << std::endl;
    std::cout << "=====================================" << std::endl;
    
    try {
        example_stock_daily_cache();
        example_integration_with_data_service();
        example_cache_policy_management();
        
        std::cout << "\n所有示例执行完成！" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}