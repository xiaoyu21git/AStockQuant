// cache_facade.h
// 缓存门面接口 - 统一缓存访问接口

#pragma once

#include "cache_manager.h"
#include <string>
#include <chrono>
#include <vector>
#include <map>
#include <memory>

namespace AStockQuantEngine {
namespace Cache {

// 缓存配置
struct CacheConfig {
    bool enabled{true};
    std::chrono::seconds defaultTtl{300};  // 默认5分钟
    
    // 本地缓存配置
    struct LocalCacheConfig {
        bool enabled{true};
        size_t maxSize{10000};
        std::chrono::seconds expireAfterAccess{3600};
        std::chrono::seconds expireAfterWrite{300};
    } localCache;
    
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
    } redisCache;
    
    // 缓存策略映射
    std::map<std::string, CachePolicy> policies;
};

// 缓存门面类 - 单例模式
class CacheFacade {
public:
    // 获取单例实例
    static CacheFacade& getInstance();
    
    // 禁止拷贝和移动
    CacheFacade(const CacheFacade&) = delete;
    CacheFacade& operator=(const CacheFacade&) = delete;
    CacheFacade(CacheFacade&&) = delete;
    CacheFacade& operator=(CacheFacade&&) = delete;
    
    // 初始化/关闭
    bool initialize(const CacheConfig& config);
    void shutdown();
    
    // 基础操作 - 模板方法支持任意类型
    template<typename T>
    bool get(const std::string& key, T& value);
    
    template<typename T>
    void set(const std::string& key, const T& value, 
             std::chrono::seconds ttl = std::chrono::seconds(0));
    
    bool remove(const std::string& key);
    bool exists(const std::string& key);
    void clear();
    
    // 批量操作
    template<typename T>
    std::map<std::string, T> getBulk(const std::vector<std::string>& keys);
    
    template<typename T>
    void setBulk(const std::map<std::string, T>& keyValues,
                 std::chrono::seconds ttl = std::chrono::seconds(0));
    
    // 统计信息
    CacheStats getStats() const;
    void resetStats();
    
    // 缓存策略
    void setPolicy(const std::string& keyPrefix, CachePolicy policy);
    CachePolicy getPolicy(const std::string& key) const;
    
    // 检查是否启用
    bool isEnabled() const;
    
    // 获取配置
    const CacheConfig& getConfig() const;
    
    // 手动刷新缓存（失效指定模式的key）
    void invalidatePattern(const std::string& pattern);
    
public:
    ~CacheFacade();
    
private:
    CacheFacade();
    
    // PIMPL实现
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// 辅助函数：生成缓存key
namespace KeyGenerator {
    // 股票基本信息
    std::string stockBasic(const std::string& symbol);
    
    // 日线行情数据
    std::string marketDaily(const std::string& symbol, 
                           const std::string& startDate, 
                           const std::string& endDate);
    
    // 实时行情
    std::string marketRealtime(const std::string& symbol);
    
    // 用户会话
    std::string userSession(const std::string& sessionId);
    
    // 策略计算结果
    std::string strategyResult(const std::string& strategyId, 
                              const std::string& symbol,
                              const std::string& date);
    
    // 数据清洗结果
    std::string cleaningResult(const std::string& requestId);
    
    // 通用key生成
    std::string generate(const std::string& category, 
                        const std::string& id1 = "",
                        const std::string& id2 = "",
                        const std::string& id3 = "");
}

} // namespace Cache
} // namespace AStockQuantEngine