// cache_facade.cpp
// 缓存门面实现

#include "cache_facade.h"
#include "local_cache_manager.h"
#include "redis_cache_manager.h"
#include "serialization.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <regex>
#include <shared_mutex>

namespace AStockQuantEngine {
namespace Cache {

// PIMPL实现类
class CacheFacade::Impl {
public:
    Impl() = default;
    ~Impl() = default;
    
    bool initialize(const CacheConfig& config) {
        std::unique_lock lock(mutex_);
        
        config_ = config;
        
        if (!config.enabled) {
            std::cout << "Cache system is disabled" << std::endl;
            return true;
        }
        
        // 初始化本地缓存
        if (config.localCache.enabled) {
            LocalCacheConfig localConfig = {
                config.localCache.enabled,
                config.localCache.maxSize,
                config.localCache.maxSize,  // initialCapacity same as maxSize
                config.localCache.expireAfterAccess,
                config.localCache.expireAfterWrite,
                true,    // recordStats
                false,   // weakKeys
                false,   // weakValues
                false    // softValues
            };
            localCacheManager_ = std::make_unique<LocalCacheManager>(localConfig);
            if (!localCacheManager_->initialize()) {
                std::cerr << "Failed to initialize local cache" << std::endl;
                localCacheManager_.reset();
            } else {
                std::cout << "Local cache initialized successfully" << std::endl;
            }
        }
        
        // 初始化Redis缓存
        if (config.redisCache.enabled) {
#ifdef NO_REDIS
            std::cout << "Redis cache disabled: hiredis not available" << std::endl;
#else
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
                std::cerr << "Failed to initialize Redis cache" << std::endl;
                redisCacheManager_.reset();
            } else {
                std::cout << "Redis cache initialized successfully" << std::endl;
            }
#endif
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
        
        if (localCacheManager_) {
            localCacheManager_->shutdown();
            localCacheManager_.reset();
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
        
        // 1. 尝试本地缓存
        if (policy.useLocalCache && localCacheManager_ && localCacheManager_->isEnabled()) {
            std::string cachedValue;
            if (localCacheManager_->get(key, cachedValue)) {
                try {
                    value = Serializer::deserialize<T>(cachedValue);
                    stats_.hits++;
                    auto end = std::chrono::high_resolution_clock::now();
                    stats_.totalGetTime += std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                    return true;
                } catch (const std::exception& e) {
                    std::cerr << "Failed to deserialize cached value: " << e.what() << std::endl;
                    // 删除损坏的缓存
                    localCacheManager_->remove(key);
                }
            }
        }
        
        // 2. 尝试Redis缓存
        if (policy.useRedisCache && redisCacheManager_ && redisCacheManager_->isEnabled()) {
            std::string cachedValue;
            if (redisCacheManager_->get(key, cachedValue)) {
                try {
                    value = Serializer::deserialize<T>(cachedValue);
                    
                    // 回写到本地缓存
                    if (policy.useLocalCache && localCacheManager_ && localCacheManager_->isEnabled()) {
                        localCacheManager_->set(key, cachedValue, policy.ttl);
                    }
                    
                    stats_.hits++;
                    auto end = std::chrono::high_resolution_clock::now();
                    stats_.totalGetTime += std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                    return true;
                } catch (const std::exception& e) {
                    std::cerr << "Failed to deserialize Redis cached value: " << e.what() << std::endl;
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
            std::cerr << "Failed to serialize value for caching: " << e.what() << std::endl;
            return;
        }
        
        // 1. 设置Redis缓存
        if (policy.useRedisCache && redisCacheManager_ && redisCacheManager_->isEnabled()) {
            if (!redisCacheManager_->set(key, serializedValue, ttl)) {
                std::cerr << "Failed to set Redis cache for key: " << key << std::endl;
            }
        }
        
        // 2. 设置本地缓存
        if (policy.useLocalCache && localCacheManager_ && localCacheManager_->isEnabled()) {
            if (!localCacheManager_->set(key, serializedValue, ttl)) {
                std::cerr << "Failed to set local cache for key: " << key << std::endl;
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
        
        // 从本地缓存删除
        if (localCacheManager_ && localCacheManager_->isEnabled()) {
            if (!localCacheManager_->remove(key)) {
                success = false;
            }
        }
        
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
        
        // 先检查本地缓存
        if (localCacheManager_ && localCacheManager_->isEnabled()) {
            if (localCacheManager_->exists(key)) {
                return true;
            }
        }
        
        // 再检查Redis缓存
        if (redisCacheManager_ && redisCacheManager_->isEnabled()) {
            return redisCacheManager_->exists(key);
        }
        
        return false;
    }
    
    void clear() {
        if (localCacheManager_ && localCacheManager_->isEnabled()) {
            localCacheManager_->clear();
        }
        
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
        
        if (localCacheManager_) {
            localCacheManager_->resetStats();
        }
        
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
        defaultPolicy.useLocalCache = config_.localCache.enabled;
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
        
        // 这里实现模式匹配的缓存失效
        // 注意：Redis支持keys命令，但生产环境慎用
        // 本地缓存需要遍历所有key
        
        std::cout << "Cache invalidation for pattern: " << pattern << std::endl;
        // 实际实现需要根据具体需求完成
    }
    
private:
    mutable std::shared_mutex mutex_;
    CacheConfig config_;
    std::unique_ptr<LocalCacheManager> localCacheManager_;
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