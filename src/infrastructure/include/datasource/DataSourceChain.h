// infrastructure/include/infrastructure/datasource/DataSourceChain.h
#pragma once

#include "IDataSource.h"
#include <memory>
#include <vector>
#include <functional>
#include <future>
#include <optional>

namespace astock {
namespace infrastructure {

// 链式调用构建器
template<typename T>
class DataSourceChainBuilder {
public:
    DataSourceChainBuilder() = default;
    
    // 添加数据源
    DataSourceChainBuilder& addDataSource(std::shared_ptr<IDataSource<T>> source);
    DataSourceChainBuilder& addCacheDataSource(std::shared_ptr<IDataSource<T>> cache);
    DataSourceChainBuilder& addDatabaseDataSource(std::shared_ptr<IDataSource<T>> database);
    DataSourceChainBuilder& addApiDataSource(std::shared_ptr<IDataSource<T>> api);
    
    // 配置参数
    DataSourceChainBuilder& withKey(const std::string& key);
    DataSourceChainBuilder& withParams(const Params& params);
    DataSourceChainBuilder& withTimeout(std::chrono::milliseconds timeout);
    DataSourceChainBuilder& withFallback(const T& fallback_data);
    DataSourceChainBuilder& withFallbackFunction(std::function<std::optional<T>()> fallback_func);
    DataSourceChainBuilder& withCacheTTL(std::chrono::milliseconds ttl);
    DataSourceChainBuilder& withRetry(int retry_count, std::chrono::milliseconds retry_delay);
    
    // 数据转换
    DataSourceChainBuilder& transform(std::function<T(const T&)> transformer);
    DataSourceChainBuilder& filter(std::function<bool(const T&)> filter);
    DataSourceChainBuilder& validate(std::function<bool(const T&)> validator);
    
    // 构建链
    std::shared_ptr<IDataSource<T>> build();
    
private:
    std::vector<std::shared_ptr<IDataSource<T>>> sources_;
    std::string key_;
    Params params_;
    std::chrono::milliseconds timeout_{5000};
    T fallback_data_{};
    std::function<std::optional<T>()> fallback_func_{nullptr};
    std::chrono::milliseconds cache_ttl_{std::chrono::seconds(300)};
    int retry_count_{3};
    std::chrono::milliseconds retry_delay_{100};
    std::vector<std::function<T(const T&)>> transformers_;
    std::vector<std::function<bool(const T&)>> filters_;
    std::vector<std::function<bool(const T&)>> validators_;
};

// 链式数据源实现
template<typename T>
class ChainDataSource : public IDataSource<T> {
public:
    ChainDataSource(const std::vector<std::shared_ptr<IDataSource<T>>>& sources,
                   const DataSourceChainBuilder<T>& builder);
    
    // IDataSource 接口实现
    std::string getName() const override;
    DataSourceType getType() const override;
    DataSourceConfig getConfig() const override;
    
    std::optional<T> fetch(const std::string& key, 
                          const Params& params = {},
                          bool use_cache = true) override;
    
    std::vector<T> fetchBatch(const std::vector<std::string>& keys,
                             const Params& params = {},
                             bool use_cache = true) override;
    
    std::future<std::optional<T>> fetchAsync(const std::string& key,
                                            const Params& params = {},
                                            bool use_cache = true) override;
    
    std::future<std::vector<T>> fetchBatchAsync(const std::vector<std::string>& keys,
                                               const Params& params = {},
                                               bool use_cache = true) override;
    
    bool validate(const T& data) const override;
    std::string validateAndGetError(const T& data) const override;
    
    T transform(const T& data, const Params& transform_params) const override;
    
    DataSourceMetrics getMetrics() const override;
    void resetMetrics() override;
    
    bool isHealthy() const override;
    std::string getHealthStatus() const override;
    
    bool configure(const DataSourceConfig& config) override;
    bool reconfigure(const std::unordered_map<std::string, std::string>& updates) override;
    
    bool clearCache() override;
    size_t cacheSize() const override;
    double cacheHitRate() const override;
    
    bool connect() override;
    bool disconnect() override;
    bool isConnected() const override;
    
    void setDataCallback(typename IDataSource<T>::DataCallback callback) override;
    
    std::string getLastError() const override;
    void setErrorHandler(std::function<void(const std::string&, 
                                           const std::string&)> handler) override;
    
    // 链式调用快捷方法
    class Executor {
    public:
        Executor(std::shared_ptr<ChainDataSource<T>> chain, 
                const std::string& key,
                const Params& params);
        
        Executor& withCache(bool use_cache);
        Executor& withTransform(std::function<T(const T&)> transformer);
        Executor& withTimeout(std::chrono::milliseconds timeout);
        
        std::optional<T> execute();
        std::future<std::optional<T>> executeAsync();
        
    private:
        std::shared_ptr<ChainDataSource<T>> chain_;
        std::string key_;
        Params params_;
        bool use_cache_{true};
        std::function<T(const T&)> transformer_{nullptr};
        std::chrono::milliseconds timeout_{5000};
    };
    
    Executor execute(const std::string& key, const Params& params = {});
    
private:
    std::vector<std::shared_ptr<IDataSource<T>>> sources_;
    DataSourceConfig config_;
    DataSourceMetrics metrics_;
    mutable std::mutex metrics_mutex_;
    
    std::string key_;
    Params params_;
    T fallback_data_;
    std::function<std::optional<T>()> fallback_func_;
    std::chrono::milliseconds timeout_;
    int retry_count_;
    std::chrono::milliseconds retry_delay_;
    
    std::vector<std::function<T(const T&)>> transformers_;
    std::vector<std::function<bool(const T&)>> filters_;
    std::vector<std::function<bool(const T&)>> validators_;
    
    typename IDataSource<T>::DataCallback data_callback_{nullptr};
    std::function<void(const std::string&, const std::string&)> error_handler_{nullptr};
    mutable std::string last_error_;
    
    std::optional<T> executeChain(const std::string& key, const Params& params, bool use_cache);
    void updateMetrics(bool success, bool cache_hit, std::chrono::milliseconds latency);
    bool shouldRetry(const std::string& error) const;
    
    void notifyCallback(const std::string& key, 
                       const std::optional<T>& data,
                       const std::string& error);
};

// 便利函数
template<typename T>
DataSourceChainBuilder<T> createDataSourceChain() {
    return DataSourceChainBuilder<T>();
}

} // namespace infrastructure
} // namespace astock