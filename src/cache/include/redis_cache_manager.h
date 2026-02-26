// redis_cache_manager.h
// Redis缓存管理器

#pragma once

#include "cache_manager.h"
#include <string>
#include <chrono>
#include <memory>
#include <vector>

namespace AStockQuantEngine {
namespace Cache {

// Redis缓存配置
struct RedisCacheConfig {
    bool enabled{true};
    std::string host{"127.0.0.1"};
    int port{6379};
    std::string password;
    int database{0};
    int connectionPoolSize{10};
    std::chrono::milliseconds connectTimeout{1000};
    std::chrono::milliseconds operationTimeout{1000};
    bool enableCluster{false};
    std::vector<std::string> clusterNodes;
    bool enableTls{false};
    std::string tlsCertPath;
    std::string tlsKeyPath;
    std::string tlsCaPath;
};

// Redis缓存管理器
class RedisCacheManager : public ICacheManager {
public:
    explicit RedisCacheManager(const RedisCacheConfig& config);
    ~RedisCacheManager() override;
    
    // ICacheManager 接口实现
    bool initialize() override;
    void shutdown() override;
    
    bool get(const std::string& key, std::string& value) override;
    bool set(const std::string& key, const std::string& value, 
             std::chrono::seconds ttl) override;
    bool remove(const std::string& key) override;
    bool exists(const std::string& key) override;
    void clear() override;
    
    std::map<std::string, std::string> getBulk(const std::vector<std::string>& keys) override;
    void setBulk(const std::map<std::string, std::string>& keyValues,
                 std::chrono::seconds ttl) override;
    
    CacheStats getStats() const override;
    void resetStats() override;
    
    void setPolicy(const std::string& keyPrefix, CachePolicy policy) override;
    CachePolicy getPolicy(const std::string& key) const override;
    
    bool isEnabled() const override;
    
    // Redis特定操作
    bool expire(const std::string& key, std::chrono::seconds ttl);
    long long increment(const std::string& key, long long delta = 1);
    long long decrement(const std::string& key, long long delta = 1);
    
    // 哈希表操作
    bool hset(const std::string& key, const std::string& field, const std::string& value);
    bool hget(const std::string& key, const std::string& field, std::string& value);
    bool hdel(const std::string& key, const std::string& field);
    std::map<std::string, std::string> hgetall(const std::string& key);
    
    // 集合操作
    bool sadd(const std::string& key, const std::string& member);
    bool srem(const std::string& key, const std::string& member);
    std::vector<std::string> smembers(const std::string& key);
    
    // 列表操作
    bool lpush(const std::string& key, const std::string& value);
    bool rpush(const std::string& key, const std::string& value);
    bool lpop(const std::string& key, std::string& value);
    bool rpop(const std::string& key, std::string& value);
    
    // 发布订阅
    bool publish(const std::string& channel, const std::string& message);
    
    // 获取连接状态
    bool isConnected() const;
    
    // 获取配置
    const RedisCacheConfig& getConfig() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace Cache
} // namespace AStockQuantEngine