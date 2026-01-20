#ifndef ASTOCK_INFRASTRUCTURE_DATASOURCE_DATASOURCEFACTORY_H
#define ASTOCK_INFRASTRUCTURE_DATASOURCE_DATASOURCEFACTORY_H

#include "IDataSource.h"
#include "DataSourceChain.h"
#include "DatabaseSource.h"
#include "../cache/ICache.h"
#include "../database/IRepository.h"
#include "../config/ConfigManager.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <any>
#include <optional>
#include <typeindex>
#include <typeinfo>

namespace astock {
namespace datasource {

// ==================== 前置声明 ====================
class DataSourceRegistry;
class DataSourceBuilder;
class DataSourceDependencyResolver;

// ==================== 数据源类型 ====================
/**
 * @brief 数据源类型枚举
 */
enum class DataSourceType {
    MEMORY_CACHE,      // 内存缓存
    REDIS_CACHE,       // Redis缓存
    DATABASE,          // 数据库
    API,               // API接口
    FILE,              // 文件
    COMPOSITE,         // 组合数据源
    CHAIN,             // 链式数据源
    CUSTOM             // 自定义数据源
};

/**
 * @brief 数据源配置基类
 */
struct DataSourceConfig {
    std::string name;                    // 数据源名称
    DataSourceType type;                 // 数据源类型
    bool enabled{true};                  // 是否启用
    int priority{0};                     // 优先级
    std::chrono::milliseconds timeout{5000}; // 超时时间
    size_t max_retries{3};               // 最大重试次数
    bool enable_cache{true};             // 是否启用缓存
    bool enable_fallback{true};          // 是否启用降级
    std::string fallback_strategy;       // 降级策略
    
    virtual ~DataSourceConfig() = default;
    
    // 验证配置
    virtual bool validate() const {
        return !name.empty() && timeout.count() > 0 && max_retries > 0;
    }
    
    // 克隆方法
    virtual std::shared_ptr<DataSourceConfig> clone() const = 0;
};

/**
 * @brief 内存缓存配置
 */
struct MemoryCacheConfig : public DataSourceConfig {
    size_t max_size{10000};              // 最大缓存项数
    cache::CachePolicy policy{cache::CachePolicy::LRU}; // 缓存策略
    std::chrono::milliseconds default_ttl{300000}; // 默认TTL（5分钟）
    
    bool validate() const override {
        return DataSourceConfig::validate() && max_size > 0;
    }
    
    std::shared_ptr<DataSourceConfig> clone() const override {
        return std::make_shared<MemoryCacheConfig>(*this);
    }
};

/**
 * @brief Redis缓存配置
 */
struct RedisCacheConfig : public DataSourceConfig {
    std::string host{"localhost"};       // Redis主机
    int port{6379};                      // Redis端口
    std::string password;                // 密码（可选）
    int database{0};                     // 数据库编号
    size_t pool_size{10};                // 连接池大小
    std::chrono::milliseconds connection_timeout{1000}; // 连接超时
    
    bool validate() const override {
        return DataSourceConfig::validate() && 
               !host.empty() && 
               port > 0 && port < 65536 &&
               pool_size > 0;
    }
    
    std::shared_ptr<DataSourceConfig> clone() const override {
        return std::make_shared<RedisCacheConfig>(*this);
    }
};

/**
 * @brief 数据库配置
 */
struct DatabaseConfig : public DataSourceConfig {
    std::string driver{"mysql"};         // 数据库驱动
    std::string host{"localhost"};       // 数据库主机
    int port{3306};                      // 数据库端口
    std::string database;                // 数据库名
    std::string username;                // 用户名
    std::string password;                // 密码
    size_t pool_size{10};                // 连接池大小
    bool enable_transaction{true};       // 是否启用事务
    std::chrono::milliseconds connection_timeout{2000}; // 连接超时
    
    bool validate() const override {
        return DataSourceConfig::validate() && 
               !driver.empty() && 
               !host.empty() && 
               port > 0 && port < 65536 &&
               !database.empty() && 
               !username.empty() && 
               pool_size > 0;
    }
    
    std::shared_ptr<DataSourceConfig> clone() const override {
        return std::make_shared<DatabaseConfig>(*this);
    }
};

/**
 * @brief API数据源配置
 */
struct ApiDataSourceConfig : public DataSourceConfig {
    std::string base_url;                // API基础URL
    std::string api_key;                 // API密钥
    std::string api_secret;              // API密钥（可选）
    std::chrono::milliseconds request_timeout{10000}; // 请求超时
    size_t max_connections{10};          // 最大连接数
    bool enable_retry{true};             // 是否启用重试
    std::vector<std::string> retry_codes{"429", "500", "502", "503"}; // 重试状态码
    
    bool validate() const override {
        return DataSourceConfig::validate() && 
               !base_url.empty() && 
               !api_key.empty() &&
               max_connections > 0;
    }
    
    std::shared_ptr<DataSourceConfig> clone() const override {
        return std::make_shared<ApiDataSourceConfig>(*this);
    }
};

/**
 * @brief 文件数据源配置
 */
struct FileDataSourceConfig : public DataSourceConfig {
    std::string file_path;               // 文件路径
    std::string format{"json"};          // 文件格式（json/csv/xml）
    std::chrono::milliseconds refresh_interval{60000}; // 刷新间隔
    bool watch_for_changes{true};        // 是否监听文件变化
    std::string encoding{"utf-8"};       // 文件编码
    
    bool validate() const override {
        return DataSourceConfig::validate() && !file_path.empty();
    }
    
    std::shared_ptr<DataSourceConfig> clone() const override {
        return std::make_shared<FileDataSourceConfig>(*this);
    }
};

/**
 * @brief 链式数据源配置
 */
struct ChainDataSourceConfig : public DataSourceConfig {
    std::vector<std::string> sources;    // 数据源名称列表
    bool stop_on_first_hit{true};        // 是否在首次命中时停止
    bool enable_parallel{false};         // 是否启用并行查询
    size_t parallel_limit{3};            // 并行查询限制
    
    bool validate() const override {
        return DataSourceConfig::validate() && !sources.empty();
    }
    
    std::shared_ptr<DataSourceConfig> clone() const override {
        return std::make_shared<ChainDataSourceConfig>(*this);
    }
};

// ==================== 数据源描述符 ====================
/**
 * @brief 数据源描述符
 */
struct DataSourceDescriptor {
    std::string name;                    // 数据源名称
    DataSourceType type;                 // 数据源类型
    std::shared_ptr<DataSourceConfig> config; // 配置
    std::vector<std::string> dependencies; // 依赖的数据源
    std::vector<std::string> provides;   // 提供的数据类型
    bool singleton{true};                // 是否单例
    bool lazy_init{true};                // 是否延迟初始化
    bool initialized{false};             // 是否已初始化
    std::chrono::system_clock::time_point created_time; // 创建时间
    std::chrono::system_clock::time_point last_used_time; // 最后使用时间
    
    // 验证描述符
    bool validate() const {
        return !name.empty() && config != nullptr && config->validate();
    }
};

// ==================== 数据源创建器接口 ====================
/**
 * @brief 数据源创建器接口
 */
class IDataSourceCreator {
public:
    virtual ~IDataSourceCreator() = default;
    
    /**
     * @brief 创建数据源
     * @param config 配置
     * @param context 创建上下文
     * @return 数据源指针
     */
    virtual std::shared_ptr<IDataSourceBase> create(
        std::shared_ptr<DataSourceConfig> config,
        const std::any& context = std::any()) = 0;
    
    /**
     * @brief 获取支持的数据源类型
     */
    virtual DataSourceType getSupportedType() const = 0;
    
    /**
     * @brief 获取配置类型
     */
    virtual std::type_index getConfigType() const = 0;
    
    /**
     * @brief 验证配置
     */
    virtual bool validateConfig(const DataSourceConfig& config) const = 0;
    
    /**
     * @brief 销毁数据源
     */
    virtual void destroy(std::shared_ptr<IDataSourceBase> source) = 0;
};

// ==================== 数据源注册表 ====================
/**
 * @brief 数据源注册表
 */
class DataSourceRegistry {
public:
    // 单例模式
    static DataSourceRegistry& instance();
    
    // 注册数据源创建器
    template<typename T, typename ConfigType>
    void registerCreator(const std::string& name, 
                        std::shared_ptr<IDataSourceCreator> creator);
    
    // 获取数据源创建器
    std::shared_ptr<IDataSourceCreator> getCreator(const std::string& name) const;
    std::shared_ptr<IDataSourceCreator> getCreator(DataSourceType type) const;
    
    // 注册数据源描述符
    void registerDescriptor(const DataSourceDescriptor& descriptor);
    
    // 获取数据源描述符
    std::optional<DataSourceDescriptor> getDescriptor(const std::string& name) const;
    std::vector<DataSourceDescriptor> getAllDescriptors() const;
    
    // 检查数据源是否存在
    bool exists(const std::string& name) const;
    
    // 注销数据源
    bool unregister(const std::string& name);
    
    // 清空注册表
    void clear();
    
private:
    DataSourceRegistry() = default;
    ~DataSourceRegistry() = default;
    
    // 禁止拷贝
    DataSourceRegistry(const DataSourceRegistry&) = delete;
    DataSourceRegistry& operator=(const DataSourceRegistry&) = delete;
    
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<IDataSourceCreator>> creators_by_name_;
    std::unordered_map<DataSourceType, std::shared_ptr<IDataSourceCreator>> creators_by_type_;
    std::unordered_map<std::string, DataSourceDescriptor> descriptors_;
};

// ==================== 数据源构建器 ====================
/**
 * @brief 数据源构建器（建造者模式）
 */
template<typename T, typename KeyType = std::string>
class DataSourceBuilder {
public:
    using DataSourcePtr = std::shared_ptr<IDataSource<T, KeyType>>;
    
    DataSourceBuilder();
    
    // 配置方法
    DataSourceBuilder& withName(const std::string& name);
    DataSourceBuilder& withType(DataSourceType type);
    DataSourceBuilder& withConfig(std::shared_ptr<DataSourceConfig> config);
    DataSourceBuilder& withCache(std::shared_ptr<cache::ICache<KeyType, T>> cache);
    DataSourceBuilder& withRepository(std::shared_ptr<database::IRepository<T>> repository);
    DataSourceBuilder& withTimeout(std::chrono::milliseconds timeout);
    DataSourceBuilder& withRetries(size_t max_retries);
    DataSourceBuilder& withFallback(std::function<std::optional<T>(const KeyType&)> fallback);
    DataSourceBuilder& withDependencies(const std::vector<std::string>& dependencies);
    DataSourceBuilder& singleton(bool is_singleton = true);
    DataSourceBuilder& lazyInit(bool lazy_init = true);
    
    // 构建方法
    DataSourcePtr build();
    std::shared_ptr<DataSourceChain<T, KeyType>> buildChain();
    
    // 从配置构建
    static DataSourceBuilder fromConfig(const std::string& config_name);
    static DataSourceBuilder fromConfig(const DataSourceDescriptor& descriptor);
    
    // 快速创建方法
    static DataSourcePtr createMemoryCache(
        const std::string& name = "memory_cache",
        size_t max_size = 10000,
        cache::CachePolicy policy = cache::CachePolicy::LRU);
    
    static DataSourcePtr createRedisCache(
        const std::string& name = "redis_cache",
        const std::string& host = "localhost",
        int port = 6379,
        int database = 0);
    
    template<typename RepositoryType>
    static DataSourcePtr createDatabaseSource(
        std::shared_ptr<RepositoryType> repository,
        const std::string& name = "database");
    
    static DataSourcePtr createApiSource(
        const std::string& name,
        const std::string& base_url,
        const std::string& api_key);
    
    static DataSourcePtr createFileSource(
        const std::string& name,
        const std::string& file_path,
        const std::string& format = "json");
    
private:
    std::string name_;
    DataSourceType type_{DataSourceType::CUSTOM};
    std::shared_ptr<DataSourceConfig> config_;
    std::shared_ptr<cache::ICache<KeyType, T>> cache_;
    std::shared_ptr<database::IRepository<T>> repository_;
    std::chrono::milliseconds timeout_{5000};
    size_t max_retries_{3};
    std::function<std::optional<T>(const KeyType&)> fallback_;
    std::vector<std::string> dependencies_;
    bool singleton_{true};
    bool lazy_init_{true};
    
    // 验证构建参数
    bool validate() const;
    
    // 创建具体的数据源
    DataSourcePtr createMemoryCacheSource();
    DataSourcePtr createRedisCacheSource();
    DataSourcePtr createDatabaseSource();
    DataSourcePtr createApiSource();
    DataSourcePtr createFileSource();
    DataSourcePtr createChainSource();
    DataSourcePtr createCustomSource();
};

// ==================== 数据源工厂 ====================
/**
 * @brief 数据源工厂主类
 */
class DataSourceFactory {
public:
    // 单例模式
    static DataSourceFactory& instance();
    
    // 生命周期管理
    void initialize();
    void shutdown();
    bool isInitialized() const;
    
    // =============== 数据源创建方法 ===============
    
    /**
     * @brief 创建数据源
     */
    template<typename T, typename KeyType = std::string>
    std::shared_ptr<IDataSource<T, KeyType>> createDataSource(
        const std::string& name,
        DataSourceType type,
        std::shared_ptr<DataSourceConfig> config);
    
    /**
     * @brief 从配置创建数据源
     */
    template<typename T, typename KeyType = std::string>
    std::shared_ptr<IDataSource<T, KeyType>> createDataSourceFromConfig(
        const std::string& config_name);
    
    /**
     * @brief 从描述符创建数据源
     */
    template<typename T, typename KeyType = std::string>
    std::shared_ptr<IDataSource<T, KeyType>> createDataSourceFromDescriptor(
        const DataSourceDescriptor& descriptor);
    
    /**
     * @brief 创建链式数据源
     */
    template<typename T, typename KeyType = std::string>
    std::shared_ptr<DataSourceChain<T, KeyType>> createChainDataSource(
        const std::string& name,
        const std::vector<std::string>& source_names,
        bool stop_on_first_hit = true);
    
    /**
     * @brief 创建组合数据源
     */
    template<typename T, typename KeyType = std::string>
    std::shared_ptr<IDataSource<T, KeyType>> createCompositeDataSource(
        const std::string& name,
        const std::vector<std::shared_ptr<IDataSource<T, KeyType>>>& sources,
        const std::string& strategy = "first_available");
    
    // =============== 数据源管理方法 ===============
    
    /**
     * @brief 获取数据源（单例）
     */
    template<typename T, typename KeyType = std::string>
    std::shared_ptr<IDataSource<T, KeyType>> getDataSource(const std::string& name);
    
    /**
     * @brief 注册数据源
     */
    template<typename T, typename KeyType = std::string>
    bool registerDataSource(const std::string& name,
                          std::shared_ptr<IDataSource<T, KeyType>> data_source,
                          bool overwrite = false);
    
    /**
     * @brief 注销数据源
     */
    bool unregisterDataSource(const std::string& name);
    
    /**
     * @brief 检查数据源是否存在
     */
    bool hasDataSource(const std::string& name) const;
    
    /**
     * @brief 获取所有数据源名称
     */
    std::vector<std::string> getAllDataSourceNames() const;
    
    /**
     * @brief 销毁数据源
     */
    bool destroyDataSource(const std::string& name);
    
    /**
     * @brief 清理所有数据源
     */
    void clearAllDataSources();
    
    // =============== 配置管理方法 ===============
    
    /**
     * @brief 加载配置
     */
    bool loadConfig(const std::string& config_path);
    bool loadConfig(const config::ConfigManager& config_manager);
    
    /**
     * @brief 保存配置
     */
    bool saveConfig(const std::string& config_path) const;
    
    /**
     * @brief 获取配置
     */
    std::shared_ptr<DataSourceConfig> getConfig(const std::string& name) const;
    
    /**
     * @brief 更新配置
     */
    bool updateConfig(const std::string& name, std::shared_ptr<DataSourceConfig> config);
    
    /**
     * @brief 获取所有配置
     */
    std::unordered_map<std::string, std::shared_ptr<DataSourceConfig>> getAllConfigs() const;
    
    // =============== 依赖解析方法 ===============
    
    /**
     * @brief 解析数据源依赖
     */
    std::vector<std::string> resolveDependencies(const std::string& source_name) const;
    
    /**
     * @brief 检查循环依赖
     */
    bool hasCircularDependency(const std::string& source_name) const;
    
    /**
     * @brief 获取依赖图
     */
    std::unordered_map<std::string, std::vector<std::string>> getDependencyGraph() const;
    
    // =============== 监控和统计方法 ===============
    
    /**
     * @brief 获取工厂统计信息
     */
    struct FactoryStats {
        size_t total_data_sources{0};
        size_t active_data_sources{0};
        size_t memory_cache_sources{0};
        size_t redis_cache_sources{0};
        size_t database_sources{0};
        size_t api_sources{0};
        size_t file_sources{0};
        size_t chain_sources{0};
        size_t custom_sources{0};
        std::chrono::system_clock::time_point startup_time;
        size_t total_creations{0};
        size_t total_destructions{0};
    };
    
    FactoryStats getStats() const;
    
    /**
     * @brief 获取数据源使用统计
     */
    struct DataSourceUsageStats {
        std::string name;
        DataSourceType type;
        size_t hit_count{0};
        size_t miss_count{0};
        size_t error_count{0};
        std::chrono::milliseconds total_response_time{0};
        std::chrono::system_clock::time_point last_used_time;
        bool is_healthy{true};
    };
    
    std::optional<DataSourceUsageStats> getUsageStats(const std::string& name) const;
    std::unordered_map<std::string, DataSourceUsageStats> getAllUsageStats() const;
    
    /**
     * @brief 生成健康报告
     */
    std::string generateHealthReport() const;
    
    /**
     * @brief 生成性能报告
     */
    std::string generatePerformanceReport() const;
    
    // =============== 工具方法 ===============
    
    /**
     * @brief 重新加载数据源
     */
    template<typename T, typename KeyType = std::string>
    bool reloadDataSource(const std::string& name);
    
    /**
     * @brief 预热数据源
     */
    template<typename T, typename KeyType = std::string>
    bool warmupDataSource(const std::string& name, 
                         const std::vector<KeyType>& keys);
    
    /**
     * @brief 测试数据源连接
     */
    bool testDataSourceConnection(const std::string& name);
    
    /**
     * @brief 验证数据源配置
     */
    bool validateDataSourceConfig(const DataSourceConfig& config) const;
    
    /**
     * @brief 注册自定义数据源创建器
     */
    template<typename CreatorType>
    bool registerCustomCreator(const std::string& name, 
                              DataSourceType type,
                              std::shared_ptr<CreatorType> creator);
    
    /**
     * @brief 设置默认配置
     */
    void setDefaultConfig(std::shared_ptr<DataSourceConfig> config);
    
    /**
     * @brief 获取默认配置
     */
    std::shared_ptr<DataSourceConfig> getDefaultConfig() const;
    
private:
    DataSourceFactory();
    ~DataSourceFactory();
    
    // 禁止拷贝
    DataSourceFactory(const DataSourceFactory&) = delete;
    DataSourceFactory& operator=(const DataSourceFactory&) = delete;
    
    // 内部实现
    struct Impl;
    std::unique_ptr<Impl> impl_;
    
    // 初始化默认创建器
    void initializeDefaultCreators();
    
    // 创建具体的数据源
    template<typename T, typename KeyType>
    std::shared_ptr<IDataSource<T, KeyType>> createDataSourceImpl(
        const std::string& name,
        DataSourceType type,
        std::shared_ptr<DataSourceConfig> config);
    
    // 管理数据源生命周期
    void manageDataSourceLifecycle(const std::string& name,
                                  std::shared_ptr<IDataSourceBase> data_source);
    
    // 更新使用统计
    void updateUsageStats(const std::string& name,
                         bool hit,
                         std::chrono::milliseconds response_time);
    
    // 检查健康状态
    void checkDataSourceHealth(const std::string& name);
};

// ==================== 预定义的创建器 ====================

/**
 * @brief 内存缓存数据源创建器
 */
template<typename T, typename KeyType = std::string>
class MemoryCacheDataSourceCreator : public IDataSourceCreator {
public:
    DataSourceType getSupportedType() const override {
        return DataSourceType::MEMORY_CACHE;
    }
    
    std::type_index getConfigType() const override {
        return typeid(MemoryCacheConfig);
    }
    
    bool validateConfig(const DataSourceConfig& config) const override {
        auto memory_config = dynamic_cast<const MemoryCacheConfig*>(&config);
        return memory_config != nullptr && memory_config->validate();
    }
    
    std::shared_ptr<IDataSourceBase> create(
        std::shared_ptr<DataSourceConfig> config,
        const std::any& context = std::any()) override;
    
    void destroy(std::shared_ptr<IDataSourceBase> source) override;
};

/**
 * @brief Redis缓存数据源创建器
 */
template<typename T, typename KeyType = std::string>
class RedisCacheDataSourceCreator : public IDataSourceCreator {
public:
    DataSourceType getSupportedType() const override {
        return DataSourceType::REDIS_CACHE;
    }
    
    std::type_index getConfigType() const override {
        return typeid(RedisCacheConfig);
    }
    
    bool validateConfig(const DataSourceConfig& config) const override {
        auto redis_config = dynamic_cast<const RedisCacheConfig*>(&config);
        return redis_config != nullptr && redis_config->validate();
    }
    
    std::shared_ptr<IDataSourceBase> create(
        std::shared_ptr<DataSourceConfig> config,
        const std::any& context = std::any()) override;
    
    void destroy(std::shared_ptr<IDataSourceBase> source) override;
};

/**
 * @brief 数据库数据源创建器
 */
template<typename T, typename KeyType = std::string>
class DatabaseDataSourceCreator : public IDataSourceCreator {
public:
    DataSourceType getSupportedType() const override {
        return DataSourceType::DATABASE;
    }
    
    std::type_index getConfigType() const override {
        return typeid(DatabaseConfig);
    }
    
    bool validateConfig(const DataSourceConfig& config) const override {
        auto db_config = dynamic_cast<const DatabaseConfig*>(&config);
        return db_config != nullptr && db_config->validate();
    }
    
    std::shared_ptr<IDataSourceBase> create(
        std::shared_ptr<DataSourceConfig> config,
        const std::any& context = std::any()) override;
    
    void destroy(std::shared_ptr<IDataSourceBase> source) override;
};

/**
 * @brief API数据源创建器
 */
template<typename T, typename KeyType = std::string>
class ApiDataSourceCreator : public IDataSourceCreator {
public:
    DataSourceType getSupportedType() const override {
        return DataSourceType::API;
    }
    
    std::type_index getConfigType() const override {
        return typeid(ApiDataSourceConfig);
    }
    
    bool validateConfig(const DataSourceConfig& config) const override {
        auto api_config = dynamic_cast<const ApiDataSourceConfig*>(&config);
        return api_config != nullptr && api_config->validate();
    }
    
    std::shared_ptr<IDataSourceBase> create(
        std::shared_ptr<DataSourceConfig> config,
        const std::any& context = std::any()) override;
    
    void destroy(std::shared_ptr<IDataSourceBase> source) override;
};

/**
 * @brief 文件数据源创建器
 */
template<typename T, typename KeyType = std::string>
class FileDataSourceCreator : public IDataSourceCreator {
public:
    DataSourceType getSupportedType() const override {
        return DataSourceType::FILE;
    }
    
    std::type_index getConfigType() const override {
        return typeid(FileDataSourceConfig);
    }
    
    bool validateConfig(const DataSourceConfig& config) const override {
        auto file_config = dynamic_cast<const FileDataSourceConfig*>(&config);
        return file_config != nullptr && file_config->validate();
    }
    
    std::shared_ptr<IDataSourceBase> create(
        std::shared_ptr<DataSourceConfig> config,
        const std::any& context = std::any()) override;
    
    void destroy(std::shared_ptr<IDataSourceBase> source) override;
};

/**
 * @brief 链式数据源创建器
 */
template<typename T, typename KeyType = std::string>
class ChainDataSourceCreator : public IDataSourceCreator {
public:
    DataSourceType getSupportedType() const override {
        return DataSourceType::CHAIN;
    }
    
    std::type_index getConfigType() const override {
        return typeid(ChainDataSourceConfig);
    }
    
    bool validateConfig(const DataSourceConfig& config) const override {
        auto chain_config = dynamic_cast<const ChainDataSourceConfig*>(&config);
        return chain_config != nullptr && chain_config->validate();
    }
    
    std::shared_ptr<IDataSourceBase> create(
        std::shared_ptr<DataSourceConfig> config,
        const std::any& context = std::any()) override;
    
    void destroy(std::shared_ptr<IDataSourceBase> source) override;
};

// ==================== 工具函数 ====================
namespace datasource_factory {
    
    /**
     * @brief 快速获取数据源
     */
    template<typename T, typename KeyType = std::string>
    std::shared_ptr<IDataSource<T, KeyType>> get(const std::string& name);
    
    /**
     * @brief 快速创建数据源
     */
    template<typename T, typename KeyType = std::string>
    std::shared_ptr<IDataSource<T, KeyType>> create(
        DataSourceType type,
        std::shared_ptr<DataSourceConfig> config);
    
    /**
     * @brief 创建股票数据源链
     */
    std::shared_ptr<DataSourceChain<market::Stock, std::string>> 
    createStockDataSourceChain(const std::string& name = "stock_chain");
    
    /**
     * @brief 创建K线数据源链
     */
    std::shared_ptr<DataSourceChain<market::KLine, std::string>> 
    createKLineDataSourceChain(const std::string& name = "kline_chain");
    
    /**
     * @brief 从YAML配置创建数据源
     */
    template<typename T, typename KeyType = std::string>
    std::shared_ptr<IDataSource<T, KeyType>> createFromYaml(const std::string& yaml_config);
    
    /**
     * @brief 从JSON配置创建数据源
     */
    template<typename T, typename KeyType = std::string>
    std::shared_ptr<IDataSource<T, KeyType>> createFromJson(const std::string& json_config);
    
    /**
     * @brief 注册全局数据源
     */
    template<typename T, typename KeyType = std::string>
    bool registerGlobalDataSource(const std::string& name,
                                 std::shared_ptr<IDataSource<T, KeyType>> data_source);
    
    /**
     * @brief 获取工厂实例
     */
    DataSourceFactory& factory();
    
} // namespace datasource_factory

} // namespace datasource
} // namespace astock
namespace std {
template<>
struct hash<DataSourceType> {
    size_t operator()(DataSourceType t) const noexcept {  // ✅ 加 noexcept 更标准
        return static_cast<size_t>(t);
    }
};
}
#endif // ASTOCK_INFRASTRUCTURE_DATASOURCE_DATASOURCEFACTORY_H