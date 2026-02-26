// local_cache_manager.h
// 本地缓存管理器 - 基于Caffeine实现

#pragma once

#include "cache_manager.h"
#include <string>
#include <chrono>
#include <memory>
#include <mutex>

namespace AStockQuantEngine {
namespace Cache {

// 本地缓存配置
struct LocalCacheConfig {
    bool enabled{true};
    size_t maxSize{10000};              // 最大条目数
    size_t initialCapacity{1000};        // 初始容量
    std::chrono::seconds expireAfterAccess{3600};  // 访问后过期时间
    std::chrono::seconds expireAfterWrite{300};    // 写入后过期时间
    bool recordStats{true};              // 是否记录统计信息
    bool weakKeys{false};                // 是否使用弱引用key
    bool weakValues{false};              // 是否使用弱引用value
    bool softValues{false};              // 是否使用软引用value
};

// 本地缓存管理器
class LocalCacheManager : public ICacheManager {
public:
    explicit LocalCacheManager(const LocalCacheConfig& config);
    ~LocalCacheManager() override;
    
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
    
    // 本地缓存特定方法
    size_t size() const;
    size_t estimatedSize() const;
    void cleanup();  // 手动清理过期条目
    
    // 获取配置
    const LocalCacheConfig& getConfig() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace Cache
} // namespace AStockQuantEngine