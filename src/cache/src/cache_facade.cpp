// cache_facade.cpp
// 缓存门面实现

#include "cache_facade.h"
#include "redis_cache_manager.h"
#include "foundation/log/logging.hpp"
#include "serialization.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <regex>
#include <shared_mutex>

namespace AStockQuantEngine {
namespace Cache {

namespace {

bool matchesGlobPattern(const std::string& pattern, const std::string& text)
{
    size_t patternIndex = 0;
    size_t textIndex = 0;
    size_t starIndex = std::string::npos;
    size_t matchIndex = 0;

    while (textIndex < text.size()) {
        if (patternIndex < pattern.size() && pattern[patternIndex] == text[textIndex]) {
            ++patternIndex;
            ++textIndex;
            continue;
        }

        if (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
            starIndex = patternIndex++;
            matchIndex = textIndex;
            continue;
        }

        if (starIndex != std::string::npos) {
            patternIndex = starIndex + 1;
            textIndex = ++matchIndex;
            continue;
        }

        return false;
    }

    while (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
        ++patternIndex;
    }

    return patternIndex == pattern.size();
}

} // namespace

// PIMPL实现类
class CacheFacade::Impl {
public:
    Impl() = default;
    ~Impl() = default;
    
    bool initialize(const CacheConfig& config) {
        std::unique_lock lock(mutex_);
        
        config_ = config;
        
        if (!config.enabled) {
            INTERNAL_INFO_STREAM << "Cache system is disabled";
            return true;
        }
        
        // 初始化Redis缓存
        if (config.redisCache.enabled) {
            RedisCacheConfig redisConfig = {
                config.redisCache.enabled,
                config.redisCache.host,
                config.redisCache.port,
                config.redisCache.password,
                config.redisCache.database,
                config.redisCache.connectionPoolSize,
                config.redisCache.connectTimeout,
                config.redisCache.operationTimeout,
                config.redisCache.enableCluster,
                config.redisCache.clusterNodes,
                false,    // enableTls
                "",       // tlsCertPath
                "",       // tlsKeyPath
                ""        // tlsCaPath
            };
            redisCacheManager_ = std::make_unique<RedisCacheManager>(redisConfig);
            if (!redisCacheManager_->initialize()) {
                INTERNAL_ERROR_STREAM << "Failed to initialize Redis cache";
                redisCacheManager_.reset();
                return false;
            } else {
                INTERNAL_INFO_STREAM << "Redis cache initialized successfully";
            }
        }
        
        initialized_ = true;
        return true;
    }
    
    void shutdown() {
        std::unique_lock lock(mutex_);
        
        if (redisCacheManager_) {
            redisCacheManager_->shutdown();
            redisCacheManager_.reset();
        }
        
        initialized_ = false;
    }
    
    template<typename T>
    bool get(const std::string& key, T& value) {
        if (!isEnabled()) {
            return false;
        }
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // 获取缓存策略
        CachePolicy policy = getPolicy(key);
        
        // 仅尝试Redis缓存
        if (policy.useRedisCache && redisCacheManager_ && redisCacheManager_->isEnabled()) {
            std::string cachedValue;
            if (redisCacheManager_->get(key, cachedValue)) {
                try {
                    value = Serializer::deserialize<T>(cachedValue);

                    stats_.hits++;
                    auto end = std::chrono::high_resolution_clock::now();
                    stats_.totalGetTime += std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                    return true;
                } catch (const std::exception& e) {
                    INTERNAL_ERROR_STREAM << "Failed to deserialize Redis cached value: " << e.what();
                    // 删除损坏的缓存
                    redisCacheManager_->remove(key);
                }
            }
        }
        
        stats_.misses++;
        auto end = std::chrono::high_resolution_clock::now();
        stats_.totalGetTime += std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        return false;
    }
    
    template<typename T>
    void set(const std::string& key, const T& value, std::chrono::seconds ttl) {
        if (!isEnabled()) {
            return;
        }
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // 获取缓存策略
        CachePolicy policy = getPolicy(key);
        if (ttl.count() == 0) {
            ttl = policy.ttl;
        }
        
        // 序列化值
        std::string serializedValue;
        try {
            serializedValue = Serializer::serialize(value);
        } catch (const std::exception& e) {
            INTERNAL_ERROR_STREAM << "Failed to serialize value for caching: " << e.what();
            return;
        }

        // 设置Redis缓存
        if (policy.useRedisCache && redisCacheManager_ && redisCacheManager_->isEnabled()) {
            if (!redisCacheManager_->set(key, serializedValue, ttl)) {
                INTERNAL_ERROR_STREAM << "Failed to set Redis cache for key: " << key;
            }
        }
        
        stats_.puts++;
        auto end = std::chrono::high_resolution_clock::now();
        stats_.totalSetTime += std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    }
    
    bool remove(const std::string& key) {
        if (!isEnabled()) {
            return false;
        }
        
        bool success = true;

        // 从Redis缓存删除
        if (redisCacheManager_ && redisCacheManager_->isEnabled()) {
            if (!redisCacheManager_->remove(key)) {
                success = false;
            }
        }
        
        if (success) {
            stats_.removes++;
        }
        
        return success;
    }
    
    bool exists(const std::string& key) {
        if (!isEnabled()) {
            return false;
        }
        
        // 仅检查Redis缓存
        if (redisCacheManager_ && redisCacheManager_->isEnabled()) {
            return redisCacheManager_->exists(key);
        }
        
        return false;
    }
    
    void clear() {
        if (redisCacheManager_ && redisCacheManager_->isEnabled()) {
            redisCacheManager_->clear();
        }
        
        resetStats();
    }
    
    template<typename T>
    std::map<std::string, T> getBulk(const std::vector<std::string>& keys) {
        std::map<std::string, T> result;
        
        if (!isEnabled() || keys.empty()) {
            return result;
        }
        
        // 批量获取逻辑
        for (const auto& key : keys) {
            T value;
            if (get(key, value)) {
                result[key] = value;
            }
        }
        
        return result;
    }
    
    template<typename T>
    void setBulk(const std::map<std::string, T>& keyValues, std::chrono::seconds ttl) {
        if (!isEnabled() || keyValues.empty()) {
            return;
        }
        
        // 批量设置逻辑
        for (const auto& [key, value] : keyValues) {
            set(key, value, ttl);
        }
    }
    
    CacheStats getStats() const {
        std::shared_lock lock(mutex_);
        return stats_;
    }
    
    void resetStats() {
        std::unique_lock lock(mutex_);
        stats_ = CacheStats{};
        
        if (redisCacheManager_) {
            redisCacheManager_->resetStats();
        }
    }
    
    void setPolicy(const std::string& keyPrefix, CachePolicy policy) {
        std::unique_lock lock(mutex_);
        config_.policies[keyPrefix] = policy;
    }
    
    CachePolicy getPolicy(const std::string& key) const {
        std::shared_lock lock(mutex_);
        
        // 查找匹配的key前缀策略
        for (const auto& [prefix, policy] : config_.policies) {
            if (key.find(prefix) == 0) {
                return policy;
            }
        }
        
        // 返回默认策略
        CachePolicy defaultPolicy;
        defaultPolicy.ttl = config_.defaultTtl;
        defaultPolicy.useLocalCache = false;
        defaultPolicy.useRedisCache = config_.redisCache.enabled;
        return defaultPolicy;
    }
    
    bool isEnabled() const {
        return initialized_ && config_.enabled;
    }
    
    const CacheConfig& getConfig() const {
        std::shared_lock lock(mutex_);
        return config_;
    }
    
    void invalidatePattern(const std::string& pattern) {
        if (!isEnabled()) {
            return;
        }
        
        if (pattern.empty()) {
            return;
        }

        if (redisCacheManager_ && redisCacheManager_->isEnabled()) {
            INTERNAL_ERROR_STREAM << "[CacheFacade] WARNING: Redis cache invalidation not implemented, pattern ignored: " << pattern;
        }
    }
    
private:
    mutable std::shared_mutex mutex_;
    CacheConfig config_;
    std::unique_ptr<RedisCacheManager> redisCacheManager_;
    CacheStats stats_;
    bool initialized_{false};
};

// 单例实例
CacheFacade& CacheFacade::getInstance() {
    static CacheFacade instance;
    return instance;
}

// 构造函数
CacheFacade::CacheFacade() = default;

// 析构函数
CacheFacade::~CacheFacade() = default;

bool CacheFacade::initialize(const CacheConfig& config) {
    if (!pImpl) {
        pImpl = std::make_unique<Impl>();
    }
    return pImpl->initialize(config);
}

void CacheFacade::shutdown() {
    if (pImpl) {
        pImpl->shutdown();
    }
}

// 模板方法显式实例化
template bool CacheFacade::Impl::get<std::string>(const std::string&, std::string&);
template void CacheFacade::Impl::set<std::string>(const std::string&, const std::string&, std::chrono::seconds);
template std::map<std::string, std::string> CacheFacade::Impl::getBulk<std::string>(const std::vector<std::string>&);
template void CacheFacade::Impl::setBulk<std::string>(const std::map<std::string, std::string>&, std::chrono::seconds);

// CacheFacade 模板方法的显式实例化
template bool CacheFacade::get<std::string>(const std::string&, std::string&);
template void CacheFacade::set<std::string>(const std::string&, const std::string&, std::chrono::seconds);
template std::map<std::string, std::string> CacheFacade::getBulk<std::string>(const std::vector<std::string>&);
template void CacheFacade::setBulk<std::string>(const std::map<std::string, std::string>&, std::chrono::seconds);

// 外部模板方法实现
template<typename T>
bool CacheFacade::get(const std::string& key, T& value) {
    if (!pImpl) return false;
    return pImpl->get(key, value);
}

template<typename T>
void CacheFacade::set(const std::string& key, const T& value, std::chrono::seconds ttl) {
    if (!pImpl) return;
    pImpl->set(key, value, ttl);
}

template<typename T>
std::map<std::string, T> CacheFacade::getBulk(const std::vector<std::string>& keys) {
    if (!pImpl) return {};
    return pImpl->getBulk<T>(keys);
}

template<typename T>
void CacheFacade::setBulk(const std::map<std::string, T>& keyValues, std::chrono::seconds ttl) {
    if (!pImpl) return;
    pImpl->setBulk(keyValues, ttl);
}

// 非模板方法实现
bool CacheFacade::remove(const std::string& key) {
    if (!pImpl) return false;
    return pImpl->remove(key);
}

bool CacheFacade::exists(const std::string& key) {
    if (!pImpl) return false;
    return pImpl->exists(key);
}

void CacheFacade::clear() {
    if (!pImpl) return;
    pImpl->clear();
}

CacheStats CacheFacade::getStats() const {
    if (!pImpl) return CacheStats{};
    return pImpl->getStats();
}

void CacheFacade::resetStats() {
    if (!pImpl) return;
    pImpl->resetStats();
}

void CacheFacade::setPolicy(const std::string& keyPrefix, CachePolicy policy) {
    if (!pImpl) return;
    pImpl->setPolicy(keyPrefix, policy);
}

CachePolicy CacheFacade::getPolicy(const std::string& key) const {
    if (!pImpl) return CachePolicy{};
    return pImpl->getPolicy(key);
}

bool CacheFacade::isEnabled() const {
    if (!pImpl) return false;
    return pImpl->isEnabled();
}

const CacheConfig& CacheFacade::getConfig() const {
    static CacheConfig emptyConfig;
    if (!pImpl) return emptyConfig;
    return pImpl->getConfig();
}

void CacheFacade::invalidatePattern(const std::string& pattern) {
    if (!pImpl) return;
    pImpl->invalidatePattern(pattern);
}

// KeyGenerator实现
namespace KeyGenerator {
    std::string stockBasic(const std::string& symbol) {
        return "stock:basic:" + symbol;
    }
    
    std::string marketDaily(const std::string& symbol, 
                           const std::string& startDate, 
                           const std::string& endDate) {
        return "market:daily:" + symbol + ":" + startDate + "-" + endDate;
    }
    
    std::string marketRealtime(const std::string& symbol) {
        return "market:realtime:" + symbol;
    }
    
    std::string userSession(const std::string& sessionId) {
        return "session:" + sessionId;
    }
    
    std::string strategyResult(const std::string& strategyId, 
                              const std::string& symbol,
                              const std::string& date) {
        return "strategy:result:" + strategyId + ":" + symbol + ":" + date;
    }
    
    std::string cleaningResult(const std::string& requestId) {
        return "cleaning:result:" + requestId;
    }
    
    std::string generate(const std::string& category, 
                        const std::string& id1,
                        const std::string& id2,
                        const std::string& id3) {
        std::string key = category;
        if (!id1.empty()) key += ":" + id1;
        if (!id2.empty()) key += ":" + id2;
        if (!id3.empty()) key += ":" + id3;
        return key;
    }
}

} // namespace Cache
} // namespace AStockQuantEngine