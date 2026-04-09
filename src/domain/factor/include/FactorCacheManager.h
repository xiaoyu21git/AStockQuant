#pragma once

#include <memory>
#include <string>
#include <chrono>
#include <map>
#include <vector>
#include "../../../cache/include/cache_facade.h"
#include "foundation/json/json_facade.h"
#include "foundation/Utils/Uuid.h"

namespace factor {

// 因子缓存键生成器
class FactorCacheKeyGenerator {
public:
    // 因子计算结果缓存键
    static std::string factorResult(const std::string& instanceId,
                                   const std::string& date);
    
    // 因子时间序列缓存键
    static std::string factorSeries(const std::string& instanceId,
                                   const std::string& startDate,
                                   const std::string& endDate);
    
    // 回测结果缓存键
    static std::string backtestResult(const std::string& instanceId,
                                     const std::string& startDate,
                                     const std::string& endDate,
                                     int forwardDays,
                                     int numGroups,
                                     const std::string& riskSignature = {});
    
    // 数据可用性缓存键
    static std::string dataAvailability(const std::string& date,
                                       const std::string& dataType);
    
    // 因子实例信息缓存键
    static std::string instanceInfo(const std::string& instanceId);
    
    // 批量生成缓存键
    static std::vector<std::string> batchFactorResults(
        const std::string& instanceId,
        const std::vector<std::string>& dates);
};

// 因子缓存管理器
class FactorCacheManager {
public:
    FactorCacheManager();
    ~FactorCacheManager() = default;
    
    // 设置缓存门面
    void setCacheFacade(std::shared_ptr<AStockQuantEngine::Cache::CacheFacade> cache);
    
    // 检查缓存是否可用
    bool isCacheAvailable() const;
    
    // ============ 因子计算结果缓存 ============
    
    // 获取因子计算结果
    bool getFactorResult(const std::string& instanceId,
                        const std::string& date,
                        foundation::json::JsonFacade& result);
    
    // 设置因子计算结果
    void setFactorResult(const std::string& instanceId,
                        const std::string& date,
                        const foundation::json::JsonFacade& result,
                        std::chrono::seconds ttl = std::chrono::seconds(0));
    
    // 批量获取因子计算结果
    std::map<std::string, foundation::json::JsonFacade> getBatchFactorResults(
        const std::string& instanceId,
        const std::vector<std::string>& dates);
    
    // 批量设置因子计算结果
    void setBatchFactorResults(
        const std::string& instanceId,
        const std::map<std::string, foundation::json::JsonFacade>& results,
        std::chrono::seconds ttl = std::chrono::seconds(0));
    
    // ============ 回测结果缓存 ============
    
    // 获取回测结果
    bool getBacktestResult(const std::string& instanceId,
                          const std::string& startDate,
                          const std::string& endDate,
                          int forwardDays,
                          int numGroups,
                          const std::string& riskSignature,
                          foundation::json::JsonFacade& result);
    
    // 设置回测结果
    void setBacktestResult(const std::string& instanceId,
                          const std::string& startDate,
                          const std::string& endDate,
                          int forwardDays,
                          int numGroups,
                          const std::string& riskSignature,
                          const foundation::json::JsonFacade& result,
                          std::chrono::seconds ttl = std::chrono::seconds(0));
    
    // ============ 数据可用性缓存 ============
    
    // 获取数据可用性状态
    bool getDataAvailability(const std::string& date,
                            const std::string& dataType,
                            foundation::json::JsonFacade& status);
    
    // 设置数据可用性状态
    void setDataAvailability(const std::string& date,
                            const std::string& dataType,
                            const foundation::json::JsonFacade& status,
                            std::chrono::seconds ttl = std::chrono::seconds(0));
    
    // ============ 因子实例信息缓存 ============
    
    // 获取因子实例信息
    bool getInstanceInfo(const std::string& instanceId,
                        foundation::json::JsonFacade& info);
    
    // 设置因子实例信息
    void setInstanceInfo(const std::string& instanceId,
                        const foundation::json::JsonFacade& info,
                        std::chrono::seconds ttl = std::chrono::seconds(0));
    
    // ============ 缓存管理 ============
    
    // 清除因子相关缓存
    void invalidateFactor(const std::string& instanceId);
    
    // 清除日期相关缓存
    void invalidateDate(const std::string& date);
    
    // 清除所有因子缓存
    void clearAll();
    
    // 获取缓存统计
    AStockQuantEngine::Cache::CacheStats getStats() const;
    
    // 缓存配置
    struct CacheConfig {
        bool enabled = true;
        std::chrono::seconds factorResultTTL = std::chrono::hours(24);      // 因子结果24小时
        std::chrono::seconds backtestResultTTL = std::chrono::hours(168);   // 回测结果7天
        std::chrono::seconds dataStatusTTL = std::chrono::hours(1);         // 数据状态1小时
        std::chrono::seconds instanceInfoTTL = std::chrono::hours(6);       // 实例信息6小时
        
        // 缓存策略
        bool useLocalCache = true;
        bool useRedisCache = true;
        size_t maxLocalCacheSize = 10000;
    };
    
    void setConfig(const CacheConfig& config);
    const CacheConfig& getConfig() const;
    
private:
    std::shared_ptr<AStockQuantEngine::Cache::CacheFacade> cacheFacade_;
    CacheConfig config_;
    
    // 获取默认TTL
    std::chrono::seconds getDefaultTTL(const std::string& cacheType) const;
    
    // 序列化/反序列化辅助
    static std::string serializeJson(const foundation::json::JsonFacade& json);
    static bool deserializeJson(const std::string& str, foundation::json::JsonFacade& json);
};

} // namespace factor