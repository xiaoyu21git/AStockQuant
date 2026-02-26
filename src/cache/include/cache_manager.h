// cache_manager.h
// 缓存管理器基类接口定义

#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <vector>
#include <map>

namespace AStockQuantEngine {
namespace Cache {

// 缓存统计数据结构
struct CacheStats {
    // 命中率统计
    uint64_t hits{0};
    uint64_t misses{0};
    uint64_t puts{0};
    uint64_t removes{0};
    
    // 时间统计
    std::chrono::microseconds totalGetTime{0};
    std::chrono::microseconds totalSetTime{0};
    
    // 内存统计
    size_t estimatedSize{0};
    size_t memoryUsage{0};
    
    // 计算命中率
    double hitRate() const {
        auto total = hits + misses;
        return total > 0 ? static_cast<double>(hits) / total : 0.0;
    }
    
    // 平均延迟（毫秒）
    double avgGetLatencyMs() const {
        auto total = hits + misses;
        return total > 0 ? totalGetTime.count() / 1000.0 / total : 0.0;
    }
    
    // 转换为字符串
    std::string toString() const;
};

// 缓存策略配置
struct CachePolicy {
    std::chrono::seconds ttl{300};  // 默认5分钟
    bool useLocalCache{true};
    bool useRedisCache{true};
    bool cacheEmptyResults{false};  // 是否缓存空结果
    int maxSize{10000};             // 最大条目数
};

// 缓存管理器基类接口
class ICacheManager {
public:
    virtual ~ICacheManager() = default;
    
    // 初始化/关闭
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    
    // 基础操作
    virtual bool get(const std::string& key, std::string& value) = 0;
    virtual bool set(const std::string& key, const std::string& value, 
                     std::chrono::seconds ttl) = 0;
    virtual bool remove(const std::string& key) = 0;
    virtual bool exists(const std::string& key) = 0;
    virtual void clear() = 0;
    
    // 批量操作
    virtual std::map<std::string, std::string> getBulk(const std::vector<std::string>& keys) = 0;
    virtual void setBulk(const std::map<std::string, std::string>& keyValues,
                         std::chrono::seconds ttl) = 0;
    
    // 统计信息
    virtual CacheStats getStats() const = 0;
    virtual void resetStats() = 0;
    
    // 缓存策略
    virtual void setPolicy(const std::string& keyPrefix, CachePolicy policy) = 0;
    virtual CachePolicy getPolicy(const std::string& key) const = 0;
    
    // 检查是否启用
    virtual bool isEnabled() const = 0;
};

} // namespace Cache
} // namespace AStockQuantEngine