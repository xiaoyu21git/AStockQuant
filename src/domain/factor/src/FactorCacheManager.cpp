#include "domain/factor/include/FactorCacheManager.h"
#include <algorithm>
#include <sstream>

namespace factor {

// ============ FactorCacheKeyGenerator 实现 ============

std::string FactorCacheKeyGenerator::factorResult(const std::string& instanceId,
                                                 const std::string& date) {
    return AStockQuantEngine::Cache::KeyGenerator::generate(
        "factor", "result", instanceId, date
    );
}

std::string FactorCacheKeyGenerator::factorSeries(const std::string& instanceId,
                                                 const std::string& startDate,
                                                 const std::string& endDate) {
    return AStockQuantEngine::Cache::KeyGenerator::generate(
        "factor", "series", instanceId, startDate + "_" + endDate
    );
}

std::string FactorCacheKeyGenerator::backtestResult(const std::string& instanceId,
                                                   const std::string& startDate,
                                                   const std::string& endDate,
                                                   int forwardDays,
                                                   int numGroups,
                                                   const std::string& riskSignature) {
    std::string params = startDate + "_" + endDate + "_" + 
                        std::to_string(forwardDays) + "_" + 
                        std::to_string(numGroups);
    if (!riskSignature.empty()) {
        params += "_" + riskSignature;
    }
    
    return AStockQuantEngine::Cache::KeyGenerator::generate(
        "factor", "backtest", instanceId, params
    );
}

std::string FactorCacheKeyGenerator::dataAvailability(const std::string& date,
                                                     const std::string& dataType) {
    return AStockQuantEngine::Cache::KeyGenerator::generate(
        "factor", "data_availability", dataType, date
    );
}

std::string FactorCacheKeyGenerator::instanceInfo(const std::string& instanceId) {
    return AStockQuantEngine::Cache::KeyGenerator::generate(
        "factor", "instance", instanceId
    );
}

std::vector<std::string> FactorCacheKeyGenerator::batchFactorResults(
    const std::string& instanceId,
    const std::vector<std::string>& dates) {
    
    std::vector<std::string> keys;
    keys.reserve(dates.size());
    
    for (const auto& date : dates) {
        keys.push_back(factorResult(instanceId, date));
    }
    
    return keys;
}

// ============ FactorCacheManager 实现 ============

FactorCacheManager::FactorCacheManager() {
    // 默认配置
    config_.enabled = true;
    config_.factorResultTTL = std::chrono::hours(24);
    config_.backtestResultTTL = std::chrono::hours(168);
    config_.dataStatusTTL = std::chrono::hours(1);
    config_.instanceInfoTTL = std::chrono::hours(6);
    config_.useLocalCache = true;
    config_.useRedisCache = true;
    config_.maxLocalCacheSize = 10000;
}

void FactorCacheManager::setCacheFacade(
    std::shared_ptr<AStockQuantEngine::Cache::CacheFacade> cache) {
    cacheFacade_ = cache;
}

bool FactorCacheManager::isCacheAvailable() const {
    return cacheFacade_ != nullptr && cacheFacade_->isEnabled();
}

bool FactorCacheManager::getFactorResult(const std::string& instanceId,
                                        const std::string& date,
                                        foundation::json::JsonFacade& result) {
    if (!isCacheAvailable()) {
        return false;
    }
    
    std::string key = FactorCacheKeyGenerator::factorResult(instanceId, date);
    std::string cachedValue;
    
    if (cacheFacade_->get(key, cachedValue)) {
        return deserializeJson(cachedValue, result);
    }
    
    return false;
}

void FactorCacheManager::setFactorResult(const std::string& instanceId,
                                        const std::string& date,
                                        const foundation::json::JsonFacade& result,
                                        std::chrono::seconds ttl) {
    if (!isCacheAvailable()) {
        return;
    }
    
    if (ttl.count() == 0) {
        ttl = getDefaultTTL("factor_result");
    }
    
    std::string key = FactorCacheKeyGenerator::factorResult(instanceId, date);
    std::string value = serializeJson(result);
    
    cacheFacade_->set(key, value, ttl);
}

std::map<std::string, foundation::json::JsonFacade> 
FactorCacheManager::getBatchFactorResults(const std::string& instanceId,
                                         const std::vector<std::string>& dates) {
    
    std::map<std::string, foundation::json::JsonFacade> results;
    
    if (!isCacheAvailable() || dates.empty()) {
        return results;
    }
    
    // 生成所有键
    std::vector<std::string> keys;
    for (const auto& date : dates) {
        keys.push_back(FactorCacheKeyGenerator::factorResult(instanceId, date));
    }
    
    // 批量获取
    auto cachedValues = cacheFacade_->getBulk<std::string>(keys);
    
    // 反序列化结果
    for (size_t i = 0; i < dates.size(); i++) {
        const auto& date = dates[i];
        const auto& key = keys[i];
        
        auto it = cachedValues.find(key);
        if (it != cachedValues.end()) {
            foundation::json::JsonFacade json;
            if (deserializeJson(it->second, json)) {
                results[date] = json;
            }
        }
    }
    
    return results;
}

void FactorCacheManager::setBatchFactorResults(
    const std::string& instanceId,
    const std::map<std::string, foundation::json::JsonFacade>& results,
    std::chrono::seconds ttl) {
    
    if (!isCacheAvailable() || results.empty()) {
        return;
    }
    
    if (ttl.count() == 0) {
        ttl = getDefaultTTL("factor_result");
    }
    
    // 准备批量设置数据
    std::map<std::string, std::string> keyValues;
    
    for (const auto& [date, result] : results) {
        std::string key = FactorCacheKeyGenerator::factorResult(instanceId, date);
        std::string value = serializeJson(result);
        keyValues[key] = value;
    }
    
    cacheFacade_->setBulk(keyValues, ttl);
}

bool FactorCacheManager::getBacktestResult(const std::string& instanceId,
                                          const std::string& startDate,
                                          const std::string& endDate,
                                          int forwardDays,
                                          int numGroups,
                                          const std::string& riskSignature,
                                          foundation::json::JsonFacade& result) {
    std::string key = FactorCacheKeyGenerator::backtestResult(
        instanceId, startDate, endDate, forwardDays, numGroups, riskSignature
    );

    {
        std::lock_guard<std::mutex> lock(backtestCacheMutex_);
        const auto memoryIt = backtestResultMemoryCache_.find(key);
        if (memoryIt != backtestResultMemoryCache_.end()) {
            return deserializeJson(memoryIt->second, result);
        }
    }

    if (!isCacheAvailable()) {
        return false;
    }
    
    std::string cachedValue;
    if (cacheFacade_->get(key, cachedValue)) {
        return deserializeJson(cachedValue, result);
    }
    
    return false;
}

void FactorCacheManager::setBacktestResult(const std::string& instanceId,
                                          const std::string& startDate,
                                          const std::string& endDate,
                                          int forwardDays,
                                          int numGroups,
                                          const std::string& riskSignature,
                                          const foundation::json::JsonFacade& result,
                                          std::chrono::seconds ttl) {
    
    std::string key = FactorCacheKeyGenerator::backtestResult(
        instanceId, startDate, endDate, forwardDays, numGroups, riskSignature
    );
    
    std::string value = serializeJson(result);

    {
        std::lock_guard<std::mutex> lock(backtestCacheMutex_);
        backtestResultMemoryCache_[key] = value;
    }

    if (!isCacheAvailable()) {
        return;
    }
    
    if (ttl.count() == 0) {
        ttl = getDefaultTTL("backtest_result");
    }

    cacheFacade_->set(key, value, ttl);
}

bool FactorCacheManager::getDataAvailability(const std::string& date,
                                            const std::string& dataType,
                                            foundation::json::JsonFacade& status) {
    
    if (!isCacheAvailable()) {
        return false;
    }
    
    std::string key = FactorCacheKeyGenerator::dataAvailability(date, dataType);
    std::string cachedValue;
    
    if (cacheFacade_->get(key, cachedValue)) {
        return deserializeJson(cachedValue, status);
    }
    
    return false;
}

void FactorCacheManager::setDataAvailability(const std::string& date,
                                            const std::string& dataType,
                                            const foundation::json::JsonFacade& status,
                                            std::chrono::seconds ttl) {
    
    if (!isCacheAvailable()) {
        return;
    }
    
    if (ttl.count() == 0) {
        ttl = getDefaultTTL("data_status");
    }
    
    std::string key = FactorCacheKeyGenerator::dataAvailability(date, dataType);
    std::string value = serializeJson(status);
    
    cacheFacade_->set(key, value, ttl);
}

bool FactorCacheManager::getInstanceInfo(const std::string& instanceId,
                                        foundation::json::JsonFacade& info) {
    
    if (!isCacheAvailable()) {
        return false;
    }
    
    std::string key = FactorCacheKeyGenerator::instanceInfo(instanceId);
    std::string cachedValue;
    
    if (cacheFacade_->get(key, cachedValue)) {
        return deserializeJson(cachedValue, info);
    }
    
    return false;
}

void FactorCacheManager::setInstanceInfo(const std::string& instanceId,
                                        const foundation::json::JsonFacade& info,
                                        std::chrono::seconds ttl) {
    
    if (!isCacheAvailable()) {
        return;
    }
    
    if (ttl.count() == 0) {
        ttl = getDefaultTTL("instance_info");
    }
    
    std::string key = FactorCacheKeyGenerator::instanceInfo(instanceId);
    std::string value = serializeJson(info);
    
    cacheFacade_->set(key, value, ttl);
}

void FactorCacheManager::invalidateFactor(const std::string& instanceId) {
    {
        std::lock_guard<std::mutex> lock(backtestCacheMutex_);
        backtestResultMemoryCache_.clear();
    }
    if (!isCacheAvailable()) {
        return;
    }
    
    // 使用模式匹配清除所有相关缓存
    std::string pattern = "factor:*:" + instanceId + ":*";
    cacheFacade_->invalidatePattern(pattern);
}

void FactorCacheManager::invalidateDate(const std::string& date) {
    {
        std::lock_guard<std::mutex> lock(backtestCacheMutex_);
        backtestResultMemoryCache_.clear();
    }
    if (!isCacheAvailable()) {
        return;
    }
    
    // 清除该日期的所有因子结果
    std::string pattern = "factor:result:*:" + date;
    cacheFacade_->invalidatePattern(pattern);
    
    // 清除该日期的数据可用性状态
    pattern = "factor:data_availability:*:" + date;
    cacheFacade_->invalidatePattern(pattern);
}

void FactorCacheManager::clearAll() {
    {
        std::lock_guard<std::mutex> lock(backtestCacheMutex_);
        backtestResultMemoryCache_.clear();
    }
    if (!isCacheAvailable()) {
        return;
    }
    
    std::string pattern = "factor:*";
    cacheFacade_->invalidatePattern(pattern);
}

AStockQuantEngine::Cache::CacheStats FactorCacheManager::getStats() const {
    if (!isCacheAvailable()) {
        return AStockQuantEngine::Cache::CacheStats();
    }
    
    return cacheFacade_->getStats();
}

void FactorCacheManager::setConfig(const CacheConfig& config) {
    config_ = config;
}

const FactorCacheManager::CacheConfig& FactorCacheManager::getConfig() const {
    return config_;
}

// ============ 私有方法实现 ============

std::chrono::seconds FactorCacheManager::getDefaultTTL(const std::string& cacheType) const {
    if (cacheType == "factor_result") {
        return config_.factorResultTTL;
    } else if (cacheType == "backtest_result") {
        return config_.backtestResultTTL;
    } else if (cacheType == "data_status") {
        return config_.dataStatusTTL;
    } else if (cacheType == "instance_info") {
        return config_.instanceInfoTTL;
    }
    
    return std::chrono::seconds(300);  // 默认5分钟
}

std::string FactorCacheManager::serializeJson(const foundation::json::JsonFacade& json) {
    return json.toString();
}

bool FactorCacheManager::deserializeJson(const std::string& str, 
                                        foundation::json::JsonFacade& json) {
    try {
        json = foundation::json::JsonFacade::parse(str);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace factor