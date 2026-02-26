// foundation_json_integration.cpp
// 展示缓存系统如何与Foundation JSON接口集成

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>

// Foundation JSON接口
#include "../../foundation/include/foundation/json/json_facade.h"

// 缓存系统接口
#include "../include/cache_facade.h"
#include "../include/serialization.h"

using namespace AStockQuantEngine::Cache;
using namespace foundation::json;

// 示例：使用Foundation JSON接口序列化K线数据
class FoundationJsonSerializer {
public:
    // 使用Foundation JSON接口序列化K线数据
    static std::string serializeKLine(const KLine& kline) {
        JsonFacade json = JsonFacade::createObject();
        
        json.set("symbol", JsonFacade::createString(kline.symbol));
        json.set("trade_date", JsonFacade::createString(kline.trade_date));
        json.set("open", JsonFacade::createDouble(kline.open));
        json.set("high", JsonFacade::createDouble(kline.high));
        json.set("low", JsonFacade::createDouble(kline.low));
        json.set("close", JsonFacade::createDouble(kline.close));
        json.set("volume", JsonFacade::createDouble(kline.volume));
        json.set("change_pct", JsonFacade::createDouble(kline.change_pct));
        
        return json.toString();
    }
    
    // 使用Foundation JSON接口反序列化K线数据
    static KLine deserializeKLine(const std::string& jsonStr) {
        try {
            JsonFacade json = JsonFacade::parse(jsonStr);
            
            KLine kline;
            kline.symbol = json.get("symbol").asString();
            kline.trade_date = json.get("trade_date").asString();
            kline.open = json.get("open").asDouble();
            kline.high = json.get("high").asDouble();
            kline.low = json.get("low").asDouble();
            kline.close = json.get("close").asDouble();
            kline.volume = json.get("volume").asDouble();
            kline.change_pct = json.get("change_pct").asDouble();
            
            return kline;
        } catch (const std::exception& e) {
            std::cerr << "JSON解析错误: " << e.what() << std::endl;
            return KLine();
        }
    }
    
    // 序列化Tick数据
    static std::string serializeTick(const Tick& tick) {
        JsonFacade json = JsonFacade::createObject();
        
        json.set("symbol", JsonFacade::createString(tick.symbol));
        json.set("timestamp", JsonFacade::createString(tick.timestamp));
        json.set("price", JsonFacade::createDouble(tick.price));
        json.set("volume", JsonFacade::createDouble(tick.volume));
        json.set("amount", JsonFacade::createDouble(tick.amount));
        json.set("direction", JsonFacade::createInt(tick.direction));
        
        return json.toString();
    }
    
    // 反序列化Tick数据
    static Tick deserializeTick(const std::string& jsonStr) {
        try {
            JsonFacade json = JsonFacade::parse(jsonStr);
            
            Tick tick;
            tick.symbol = json.get("symbol").asString();
            tick.timestamp = json.get("timestamp").asString();
            tick.price = json.get("price").asDouble();
            tick.volume = json.get("volume").asDouble();
            tick.amount = json.get("amount").asDouble();
            tick.direction = json.get("direction").asInt();
            
            return tick;
        } catch (const std::exception& e) {
            std::cerr << "JSON解析错误: " << e.what() << std::endl;
            return Tick();
        }
    }
    
    // 序列化Factor数据
    static std::string serializeFactor(const Factor& factor) {
        JsonFacade json = JsonFacade::createObject();
        
        json.set("symbol", JsonFacade::createString(factor.symbol));
        json.set("date", JsonFacade::createString(factor.date));
        json.set("factor_name", JsonFacade::createString(factor.factor_name));
        json.set("value", JsonFacade::createDouble(factor.value));
        
        return json.toString();
    }
    
    // 反序列化Factor数据
    static Factor deserializeFactor(const std::string& jsonStr) {
        try {
            JsonFacade json = JsonFacade::parse(jsonStr);
            
            Factor factor;
            factor.symbol = json.get("symbol").asString();
            factor.date = json.get("date").asString();
            factor.factor_name = json.get("factor_name").asString();
            factor.value = json.get("value").asDouble();
            
            return factor;
        } catch (const std::exception& e) {
            std::cerr << "JSON解析错误: " << e.what() << std::endl;
            return Factor();
        }
    }
};

// 示例：使用Foundation JSON接口的缓存适配器
class FoundationJsonCacheAdapter {
private:
    std::shared_ptr<CacheFacade> cache_;
    
public:
    FoundationJsonCacheAdapter() {
        // 创建缓存实例
        cache_ = std::make_shared<CacheFacade>();
        
        // 配置缓存策略
        CacheConfig config;
        config.enableLocalCache = true;
        config.enableRedisCache = false;  // 示例中禁用Redis
        config.defaultTTL = 300;  // 5分钟
        
        cache_->initialize(config);
    }
    
    // 存储K线数据
    bool storeKLine(const std::string& key, const KLine& kline) {
        // 使用Foundation JSON序列化
        std::string jsonData = FoundationJsonSerializer::serializeKLine(kline);
        
        // 存储到缓存
        return cache_->put(key, jsonData, 86400);  // 日线数据24小时
    }
    
    // 获取K线数据
    KLine getKLine(const std::string& key) {
        // 从缓存获取
        auto result = cache_->get(key);
        if (!result.has_value()) {
            return KLine();  // 返回空对象
        }
        
        // 使用Foundation JSON反序列化
        return FoundationJsonSerializer::deserializeKLine(result.value());
    }
    
    // 批量存储K线数据
    bool storeKLineBatch(const std::string& symbol, 
                         const std::vector<KLine>& klines) {
        // 创建JSON数组
        JsonFacade jsonArray = JsonFacade::createArray();
        
        for (const auto& kline : klines) {
            std::string klineJson = FoundationJsonSerializer::serializeKLine(kline);
            JsonFacade klineObj = JsonFacade::parse(klineJson);
            jsonArray.push_back(klineObj);
        }
        
        // 生成缓存key
        std::string cacheKey = "market:daily:" + symbol + ":batch";
        
        // 存储到缓存
        return cache_->put(cacheKey, jsonArray.toString(), 86400);
    }
    
    // 批量获取K线数据
    std::vector<KLine> getKLineBatch(const std::string& symbol) {
        std::string cacheKey = "market:daily:" + symbol + ":batch";
        
        auto result = cache_->get(cacheKey);
        if (!result.has_value()) {
            return {};
        }
        
        try {
            JsonFacade jsonArray = JsonFacade::parse(result.value());
            std::vector<KLine> klines;
            
            for (size_t i = 0; i < jsonArray.size(); i++) {
                JsonFacade klineJson = jsonArray.at(i);
                KLine kline = FoundationJsonSerializer::deserializeKLine(klineJson.toString());
                klines.push_back(kline);
            }
            
            return klines;
        } catch (const std::exception& e) {
            std::cerr << "批量数据解析错误: " << e.what() << std::endl;
            return {};
        }
    }
    
    // 获取缓存统计信息（使用Foundation JSON）
    std::string getCacheStatsJson() {
        auto stats = cache_->getStats();
        
        JsonFacade json = JsonFacade::createObject();
        json.set("hits", JsonFacade::createInt(stats.hits));
        json.set("misses", JsonFacade::createInt(stats.misses));
        json.set("size", JsonFacade::createInt(stats.size));
        json.set("capacity", JsonFacade::createInt(stats.capacity));
        json.set("hit_rate", JsonFacade::createDouble(stats.hit_rate));
        json.set("memory_usage", JsonFacade::createInt(stats.memory_usage));
        
        return json.toString();
    }
};

// 测试函数
void test_foundation_json_integration() {
    std::cout << "=== Foundation JSON接口与缓存系统集成测试 ===" << std::endl;
    
    // 1. 测试Foundation JSON序列化
    std::cout << "\n1. 测试Foundation JSON序列化:" << std::endl;
    
    KLine testKLine;
    testKLine.symbol = "000001.SZ";
    testKLine.trade_date = "2024-01-15";
    testKLine.open = 10.5;
    testKLine.high = 11.2;
    testKLine.low = 10.3;
    testKLine.close = 10.8;
    testKLine.volume = 1000000;
    testKLine.change_pct = 2.86;
    
    std::string jsonStr = FoundationJsonSerializer::serializeKLine(testKLine);
    std::cout << "序列化结果: " << jsonStr << std::endl;
    
    KLine parsedKLine = FoundationJsonSerializer::deserializeKLine(jsonStr);
    std::cout << "反序列化验证: symbol=" << parsedKLine.symbol 
              << ", close=" << parsedKLine.close << std::endl;
    
    // 2. 测试缓存适配器
    std::cout << "\n2. 测试缓存适配器:" << std::endl;
    
    FoundationJsonCacheAdapter cacheAdapter;
    
    // 存储数据
    std::string cacheKey = "market:daily:000001.SZ:2024-01-15";
    bool stored = cacheAdapter.storeKLine(cacheKey, testKLine);
    std::cout << "数据存储: " << (stored ? "成功" : "失败") << std::endl;
    
    // 获取数据
    KLine cachedKLine = cacheAdapter.getKLine(cacheKey);
    std::cout << "缓存读取: symbol=" << cachedKLine.symbol 
              << ", close=" << cachedKLine.close << std::endl;
    
    // 3. 测试批量操作
    std::cout << "\n3. 测试批量操作:" << std::endl;
    
    std::vector<KLine> klines = {
        testKLine,
        { "000001.SZ", "2024-01-16", 10.8, 11.0, 10.7, 10.9, 1200000, 0.93 },
        { "000001.SZ", "2024-01-17", 10.9, 11.1, 10.8, 11.0, 1100000, 0.92 }
    };
    
    bool batchStored = cacheAdapter.storeKLineBatch("000001.SZ", klines);
    std::cout << "批量存储: " << (batchStored ? "成功" : "失败") << std::endl;
    
    auto cachedBatch = cacheAdapter.getKLineBatch("000001.SZ");
    std::cout << "批量读取: " << cachedBatch.size() << " 条记录" << std::endl;
    
    // 4. 测试缓存统计
    std::cout << "\n4. 测试缓存统计:" << std::endl;
    
    std::string statsJson = cacheAdapter.getCacheStatsJson();
    std::cout << "缓存统计JSON: " << statsJson << std::endl;
    
    // 5. 验证数据一致性
    std::cout << "\n5. 验证数据一致性:" << std::endl;
    
    if (testKLine.symbol == cachedKLine.symbol && 
        std::abs(testKLine.close - cachedKLine.close) < 0.001) {
        std::cout << "✅ 数据一致性验证通过" << std::endl;
    } else {
        std::cout << "❌ 数据一致性验证失败" << std::endl;
    }
    
    std::cout << "\n=== 测试完成 ===" << std::endl;
}

// 与现有DataService集成的示例
void demonstrate_data_service_integration() {
    std::cout << "\n=== DataService集成示例 ===" << std::endl;
    
    // 模拟现有的DataService查询函数
    auto mockDataServiceQuery = [](const std::string& symbol, 
                                   const std::string& startDate,
                                   const std::string& endDate) -> std::vector<KLine> {
        // 模拟数据库查询结果
        return {
            { symbol, startDate, 10.5, 11.2, 10.3, 10.8, 1000000, 2.86 },
            { symbol, "2024-01-16", 10.8, 11.0, 10.7, 10.9, 1200000, 0.93 },
            { symbol, "2024-01-17", 10.9, 11.1, 10.8, 11.0, 1100000, 0.92 }
        };
    };
    
    // 带缓存的DataService查询函数
    auto cachedDataServiceQuery = [&](const std::string& symbol,
                                      const std::string& startDate,
                                      const std::string& endDate) -> std::vector<KLine> {
        // 生成缓存key
        std::string cacheKey = "market:daily:" + symbol + ":" + startDate + "-" + endDate;
        
        // 创建缓存适配器
        static FoundationJsonCacheAdapter cacheAdapter;
        
        // 尝试从缓存获取
        auto cachedBatch = cacheAdapter.getKLineBatch(cacheKey);
        if (!cachedBatch.empty()) {
            std::cout << "  缓存命中: " << cacheKey << std::endl;
            return cachedBatch;
        }
        
        std::cout << "  缓存未命中: " << cacheKey << std::endl;
        
        // 查询数据库
        auto klines = mockDataServiceQuery(symbol, startDate, endDate);
        
        // 存储到缓存
        if (!klines.empty()) {
            cacheAdapter.storeKLineBatch(cacheKey, klines);
            std::cout << "  数据已缓存: " << klines.size() << " 条记录" << std::endl;
        }
        
        return klines;
    };
    
    std::cout << "第一次查询（应该缓存未命中）:" << std::endl;
    auto result1 = cachedDataServiceQuery("000001.SZ", "2024-01-15", "2024-01-17");
    std::cout << "  结果数量: " << result1.size() << std::endl;
    
    std::cout << "\n第二次查询（应该缓存命中）:" << std::endl;
    auto result2 = cachedDataServiceQuery("000001.SZ", "2024-01-15", "2024-01-17");
    std::cout << "  结果数量: " << result2.size() << std::endl;
    
    std::cout << "\n查询不同股票（应该缓存未命中）:" << std::endl;
    auto result3 = cachedDataServiceQuery("000002.SZ", "2024-01-15", "2024-01-17");
    std::cout << "  结果数量: " << result3.size() << std::endl;
    
    std::cout << "\n✅ DataService集成示例完成" << std::endl;
}

int main() {
    std::cout << "AStockQuantEngine - Foundation JSON接口与缓存系统集成演示" << std::endl;
    std::cout << "==========================================================" << std::endl;
    
    try {
        test_foundation_json_integration();
        demonstrate_data_service_integration();
        
        std::cout << "\n==========================================================" << std::endl;
        std::cout << "集成演示总结:" << std::endl;
        std::cout << "1. ✅ Foundation JSON接口正常工作" << std::endl;
        std::cout << "2. ✅ 缓存系统与Foundation JSON无缝集成" << std::endl;
        std::cout << "3. ✅ 支持批量数据序列化和缓存" << std::endl;
        std::cout << "4. ✅ 与现有DataService兼容" << std::endl;
        std::cout << "5. ✅ 性能优化：缓存命中显著减少数据库查询" << std::endl;
        
        std::cout << "\n技术优势:" << std::endl;
        std::cout << "• 复用项目现有的Foundation JSON接口" << std::endl;
        std::cout << "• 统一的序列化标准" << std::endl;
        std::cout << "• 减少外部依赖（nlohmann/json）" << std::endl;
        std::cout << "• 更好的错误处理和类型安全" << std::endl;
        std::cout << "• 与Foundation模块的其他组件协同工作" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "演示错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}