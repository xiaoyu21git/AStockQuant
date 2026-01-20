// infrastructure/include/infrastructure/datasource/DataSourceContext.h
#pragma once

#include "IDataSource.h"
#include "DataSourceChain.h"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <string>

namespace astock {
namespace infrastructure {

// 数据源上下文管理器
class DataSourceContext {
public:
    static DataSourceContext& instance();
    
    // 禁止拷贝和移动
    DataSourceContext(const DataSourceContext&) = delete;
    DataSourceContext& operator=(const DataSourceContext&) = delete;
    DataSourceContext(DataSourceContext&&) = delete;
    DataSourceContext& operator=(DataSourceContext&&) = delete;
    
    // 数据源注册和管理
    template<typename T>
    bool registerDataSource(const std::string& name,
                           std::shared_ptr<IDataSource<T>> data_source);
    
    template<typename T>
    std::shared_ptr<IDataSource<T>> getDataSource(const std::string& name);
    
    template<typename T>
    bool removeDataSource(const std::string& name);
    
    std::vector<std::string> getDataSourceNames() const;
    
    // 链式调用管理
    template<typename T>
    DataSourceChainBuilder<T> createChain(const std::string& chain_name = "");
    
    template<typename T>
    std::shared_ptr<IDataSource<T>> getChain(const std::string& chain_name);
    
    template<typename T>
    bool saveChain(const std::string& chain_name,
                  const DataSourceChainBuilder<T>& builder);
    
    template<typename T>
    bool loadChain(const std::string& chain_name);
    
    // 工厂模式
    template<typename T>
    std::shared_ptr<IDataSource<T>> createDataSource(const DataSourceConfig& config);
    
    // 预定义链配置
    struct PredefinedChain {
        std::string name;
        std::vector<std::string> source_names;
        DataSourceConfig config;
        std::unordered_map<std::string, std::string> mappings;
    };
    
    bool addPredefinedChain(const PredefinedChain& chain);
    std::shared_ptr<IDataSource<void>> getPredefinedChain(const std::string& name);
    
    // 配置管理
    bool loadConfig(const std::string& config_file);
    bool saveConfig(const std::string& config_file);
    
    // 监控和统计
    struct ContextStats {
        size_t total_data_sources = 0;
        size_t total_chains = 0;
        std::unordered_map<std::string, DataSourceMetrics> source_metrics;
        std::unordered_map<std::string, size_t> chain_usage;
    };
    
    ContextStats getStats() const;
    void resetAllMetrics();
    
    // 健康检查
    bool checkAllHealth();
    std::vector<std::string> getUnhealthySources();
    
    // 事件订阅
    using SourceEvent = std::function<void(const std::string& source_name,
                                          const std::string& event_type,
                                          const std::string& message)>;
    
    void subscribeToEvents(SourceEvent callback);
    void unsubscribeFromEvents();
    
private:
    DataSourceContext();
    ~DataSourceContext();
    
    // 类型擦除包装
    class IDataSourceWrapper {
    public:
        virtual ~IDataSourceWrapper() = default;
        virtual DataSourceMetrics getMetrics() const = 0;
        virtual DataSourceConfig getConfig() const = 0;
        virtual bool isHealthy() const = 0;
        virtual void resetMetrics() = 0;
        virtual bool clearCache() = 0;
        virtual size_t cacheSize() const = 0;
    };
    
    template<typename T>
    class DataSourceWrapper : public IDataSourceWrapper {
    public:
        DataSourceWrapper(std::shared_ptr<IDataSource<T>> source) : source_(source) {}
        
        DataSourceMetrics getMetrics() const override { return source_->getMetrics(); }
        DataSourceConfig getConfig() const override { return source_->getConfig(); }
        bool isHealthy() const override { return source_->isHealthy(); }
        void resetMetrics() override { source_->resetMetrics(); }
        bool clearCache() override { return source_->clearCache(); }
        size_t cacheSize() const override { return source_->cacheSize(); }
        
        std::shared_ptr<IDataSource<T>> getSource() { return source_; }
        
    private:
        std::shared_ptr<IDataSource<T>> source_;
    };
    
    std::unordered_map<std::string, std::shared_ptr<IDataSourceWrapper>> data_sources_;
    std::unordered_map<std::string, std::shared_ptr<IDataSourceWrapper>> chains_;
    std::unordered_map<std::string, PredefinedChain> predefined_chains_;
    
    mutable std::mutex mutex_;
    
    SourceEvent event_callback_{nullptr};
    
    void notifyEvent(const std::string& source_name,
                    const std::string& event_type,
                    const std::string& message);
    
    // 内部工厂
    template<typename T>
    std::shared_ptr<IDataSource<T>> createCacheDataSource(const DataSourceConfig& config);
    
    template<typename T>
    std::shared_ptr<IDataSource<T>> createDatabaseDataSource(const DataSourceConfig& config);
    
    template<typename T>
    std::shared_ptr<IDataSource<T>> createApiDataSource(const DataSourceConfig& config);
    
    template<typename T>
    std::shared_ptr<IDataSource<T>> createFileDataSource(const DataSourceConfig& config);
};

} // namespace infrastructure
} // namespace astock