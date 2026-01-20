// infrastructure/include/infrastructure/datasource/IDataSource.h
#pragma once

#include <memory>
#include <string>
#include <optional>
#include <vector>
#include <unordered_map>
#include <functional>
#include <chrono>

namespace astock {
namespace infrastructure {

// 数据源类型
enum class DataSourceType {
    CACHE,
    DATABASE,
    API,
    FILE,
    CALCULATED,
    COMPOSITE
};

// 查询参数
using Params = std::unordered_map<std::string, std::string>;

// 数据源指标
struct DataSourceMetrics {
    size_t total_queries = 0;
    size_t successful_queries = 0;
    size_t failed_queries = 0;
    size_t cache_hits = 0;
    size_t cache_misses = 0;
    std::chrono::milliseconds average_latency{0};
    std::chrono::milliseconds max_latency{0};
    std::chrono::milliseconds min_latency{0};
    
    double successRate() const {
        return total_queries > 0 ? static_cast<double>(successful_queries) / total_queries : 0.0;
    }
    
    double hitRate() const {
        return (cache_hits + cache_misses) > 0 ? 
            static_cast<double>(cache_hits) / (cache_hits + cache_misses) : 0.0;
    }
};

// 数据源配置
struct DataSourceConfig {
    std::string name;
    DataSourceType type;
    std::unordered_map<std::string, std::string> parameters;
    int timeout_ms = 5000;
    int retry_count = 3;
    int retry_delay_ms = 100;
    bool enabled = true;
    bool cache_enabled = true;
    std::chrono::milliseconds cache_ttl = std::chrono::seconds(300);
    int max_connections = 10;
    int max_queue_size = 100;
};

// 数据源接口
template<typename T>
class IDataSource {
public:
    virtual ~IDataSource() = default;
    
    // 基本信息
    virtual std::string getName() const = 0;
    virtual DataSourceType getType() const = 0;
    virtual DataSourceConfig getConfig() const = 0;
    
    // 数据获取
    virtual std::optional<T> fetch(const std::string& key, 
                                  const Params& params = {},
                                  bool use_cache = true) = 0;
    
    virtual std::vector<T> fetchBatch(const std::vector<std::string>& keys,
                                     const Params& params = {},
                                     bool use_cache = true) = 0;
    
    // 异步获取
    virtual std::future<std::optional<T>> fetchAsync(const std::string& key,
                                                    const Params& params = {},
                                                    bool use_cache = true) = 0;
    
    virtual std::future<std::vector<T>> fetchBatchAsync(const std::vector<std::string>& keys,
                                                       const Params& params = {},
                                                       bool use_cache = true) = 0;
    
    // 数据验证
    virtual bool validate(const T& data) const = 0;
    virtual std::string validateAndGetError(const T& data) const = 0;
    
    // 数据转换
    virtual T transform(const T& data, const Params& transform_params) const = 0;
    
    // 性能指标
    virtual DataSourceMetrics getMetrics() const = 0;
    virtual void resetMetrics() = 0;
    
    // 健康检查
    virtual bool isHealthy() const = 0;
    virtual std::string getHealthStatus() const = 0;
    
    // 配置管理
    virtual bool configure(const DataSourceConfig& config) = 0;
    virtual bool reconfigure(const std::unordered_map<std::string, std::string>& updates) = 0;
    
    // 缓存管理
    virtual bool clearCache() = 0;
    virtual size_t cacheSize() const = 0;
    virtual double cacheHitRate() const = 0;
    
    // 连接管理
    virtual bool connect() = 0;
    virtual bool disconnect() = 0;
    virtual bool isConnected() const = 0;
    
    // 事件回调
    using DataCallback = std::function<void(const std::string& key, 
                                           const std::optional<T>& data,
                                           const std::string& error)>;
    
    virtual void setDataCallback(DataCallback callback) = 0;
    
    // 错误处理
    virtual std::string getLastError() const = 0;
    virtual void setErrorHandler(std::function<void(const std::string&, 
                                                   const std::string&)> handler) = 0;
};

} // namespace infrastructure
} // namespace astock