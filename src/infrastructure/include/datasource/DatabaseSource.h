#ifndef ASTOCK_INFRASTRUCTURE_DATASOURCE_DATABASESOURCE_H
#define ASTOCK_INFRASTRUCTURE_DATASOURCE_DATABASESOURCE_H

#include "IDataSource.h"
#include "../database/IRepository.h"
#include "../cache/ICache.h"
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <optional>
#include <functional>
#include <unordered_map>

namespace astock {
namespace datasource {

// ==================== 前置声明 ====================
class DatabaseSourceConfig;
class DatabaseQueryBuilder;
class DatabaseHealthChecker;

// ==================== 数据库源配置 ====================
/**
 * @brief 数据库源配置
 */
struct DatabaseSourceConfig {
    // 基本配置
    std::string name{"database"};
    std::chrono::milliseconds timeout{5000};      // 查询超时时间
    std::chrono::milliseconds slow_query_threshold{1000}; // 慢查询阈值
    
    // 连接配置
    size_t max_retries{3};                        // 最大重试次数
    std::chrono::milliseconds retry_delay{100};   // 重试延迟
    bool enable_connection_pool{true};            // 启用连接池
    size_t connection_pool_size{10};              // 连接池大小
    
    // 缓存配置
    bool enable_query_cache{true};                // 启用查询缓存
    std::chrono::milliseconds query_cache_ttl{60000}; // 查询缓存TTL
    size_t max_cached_queries{1000};              // 最大缓存查询数
    
    // 性能配置
    size_t batch_size{100};                       // 批量操作大小
    bool enable_batch_operations{true};           // 启用批量操作
    bool enable_prepared_statements{true};        // 启用预处理语句
    
    // 监控配置
    bool enable_metrics{true};                     // 启用指标收集
    bool enable_slow_query_log{true};              // 启用慢查询日志
    bool enable_error_log{true};                   // 启用错误日志
    
    // 降级配置
    bool enable_fallback{true};                    // 启用降级策略
    std::chrono::milliseconds circuit_breaker_timeout{30000}; // 熔断器超时
    size_t circuit_breaker_threshold{10};          // 熔断器阈值
    
    // 验证配置
    bool validate() const {
        return timeout.count() > 0 && 
               connection_pool_size > 0 &&
               max_retries > 0;
    }
};

// ==================== 数据库查询状态 ====================
/**
 * @brief 数据库查询状态
 */
enum class DatabaseQueryStatus {
    SUCCESS,               // 查询成功
    TIMEOUT,               // 查询超时
    CONNECTION_ERROR,      // 连接错误
    QUERY_ERROR,           // 查询错误
    NO_DATA_FOUND,         // 没有找到数据
    SLOW_QUERY,            // 慢查询
    CIRCUIT_BREAKER_OPEN   // 熔断器打开
};

// ==================== 数据库查询结果 ====================
/**
 * @brief 数据库查询结果
 */
template<typename T>
struct DatabaseQueryResult {
    DatabaseQueryStatus status{DatabaseQueryStatus::SUCCESS};
    std::optional<T> data{std::nullopt};
    std::chrono::milliseconds execution_time{0};
    std::string error_message{};
    size_t retry_count{0};
    bool from_cache{false};
    std::chrono::system_clock::time_point timestamp;
    
    // 是否成功
    bool success() const { return status == DatabaseQueryStatus::SUCCESS && data.has_value(); }
    
    // 是否应该重试
    bool shouldRetry() const {
        return status == DatabaseQueryStatus::TIMEOUT || 
               status == DatabaseQueryStatus::CONNECTION_ERROR;
    }
};

// ==================== 数据库查询指标 ====================
/**
 * @brief 数据库查询指标
 */
struct DatabaseQueryMetrics {
    // 计数器
    std::atomic<size_t> total_queries{0};
    std::atomic<size_t> successful_queries{0};
    std::atomic<size_t> failed_queries{0};
    std::atomic<size_t> timeout_queries{0};
    std::atomic<size_t> slow_queries{0};
    std::atomic<size_t> cached_queries{0};
    
    // 时间统计
    std::atomic<std::chrono::milliseconds::rep> total_execution_time{0};
    std::atomic<std::chrono::milliseconds::rep> max_execution_time{0};
    std::atomic<std::chrono::milliseconds::rep> min_execution_time{0};
    
    // 连接统计
    std::atomic<size_t> active_connections{0};
    std::atomic<size_t> max_active_connections{0};
    std::atomic<size_t> total_connections{0};
    
    // 重置指标
    void reset() {
        total_queries = 0;
        successful_queries = 0;
        failed_queries = 0;
        timeout_queries = 0;
        slow_queries = 0;
        cached_queries = 0;
        total_execution_time = 0;
        max_execution_time = 0;
        min_execution_time = 0;
        active_connections = 0;
        max_active_connections = 0;
        total_connections = 0;
    }
    
    // 获取平均执行时间
    std::chrono::milliseconds getAverageExecutionTime() const {
        auto total = total_queries.load();
        if (total == 0) return std::chrono::milliseconds(0);
        return std::chrono::milliseconds(total_execution_time.load() / total);
    }
};

// ==================== 数据库查询构建器 ====================
/**
 * @brief 数据库查询构建器
 */
class DatabaseQueryBuilder {
public:
    // 构造函数
    explicit DatabaseQueryBuilder(const std::string& table_name = "");
    
    // 查询构建方法
    DatabaseQueryBuilder& select(const std::vector<std::string>& columns);
    DatabaseQueryBuilder& select(const std::string& column);
    DatabaseQueryBuilder& where(const std::string& condition);
    DatabaseQueryBuilder& andWhere(const std::string& condition);
    DatabaseQueryBuilder& orWhere(const std::string& condition);
    DatabaseQueryBuilder& orderBy(const std::string& column, bool ascending = true);
    DatabaseQueryBuilder& limit(size_t count);
    DatabaseQueryBuilder& offset(size_t count);
    DatabaseQueryBuilder& groupBy(const std::string& column);
    DatabaseQueryBuilder& having(const std::string& condition);
    
    // 构建SQL
    std::string buildSelect() const;
    std::string buildCount() const;
    
    // 获取参数
    std::vector<std::string> getParameters() const;
    
    // 生成缓存键
    std::string generateCacheKey(const std::string& prefix = "") const;
    
private:
    std::string table_name_;
    std::vector<std::string> columns_;
    std::vector<std::string> where_clauses_;
    std::vector<std::string> order_by_clauses_;
    std::vector<std::string> group_by_clauses_;
    std::string having_clause_;
    std::optional<size_t> limit_;
    std::optional<size_t> offset_;
    std::vector<std::string> parameters_;
    
    // 逻辑操作符
    enum class LogicalOp { AND, OR };
    std::vector<LogicalOp> where_operators_;
    
    // 构建WHERE子句
    std::string buildWhereClause() const;
};

// ==================== 数据库健康检查器 ====================
/**
 * @brief 数据库健康检查器
 */
class DatabaseHealthChecker {
public:
    // 健康状态
    enum class HealthStatus {
        HEALTHY,           // 健康
        DEGRADED,          // 降级
        UNHEALTHY,         // 不健康
        UNKNOWN            // 未知
    };
    
    // 健康检查结果
    struct HealthCheckResult {
        HealthStatus status{HealthStatus::UNKNOWN};
        std::chrono::milliseconds response_time{0};
        std::string error_message{};
        std::chrono::system_clock::time_point last_check_time;
        size_t consecutive_failures{0};
        
        bool isHealthy() const { return status == HealthStatus::HEALTHY; }
        bool isDegraded() const { return status == HealthStatus::DEGRADED; }
    };
    
    // 构造函数
    explicit DatabaseHealthChecker(std::shared_ptr<database::IDatabaseConnection> connection);
    
    // 执行健康检查
    HealthCheckResult checkHealth();
    
    // 配置
    void setCheckInterval(std::chrono::milliseconds interval);
    void setTimeout(std::chrono::milliseconds timeout);
    void setHealthyThreshold(size_t threshold);
    void setUnhealthyThreshold(size_t threshold);
    
    // 获取状态
    HealthStatus getCurrentStatus() const;
    std::chrono::milliseconds getAverageResponseTime() const;
    
private:
    std::shared_ptr<database::IDatabaseConnection> connection_;
    std::chrono::milliseconds check_interval_{5000};
    std::chrono::milliseconds timeout_{1000};
    size_t healthy_threshold_{3};
    size_t unhealthy_threshold_{5};
    
    mutable std::shared_mutex mutex_;
    HealthCheckResult last_result_;
    std::vector<std::chrono::milliseconds> response_time_history_;
    size_t history_size_{10};
    
    // 执行实际的健康检查
    HealthCheckResult performHealthCheck();
    
    // 更新健康状态
    void updateHealthStatus(const HealthCheckResult& result);
};

// ==================== 数据库熔断器 ====================
/**
 * @brief 数据库熔断器（Circuit Breaker）
 */
class DatabaseCircuitBreaker {
public:
    // 熔断器状态
    enum class State {
        CLOSED,      // 关闭状态：正常处理请求
        OPEN,        // 打开状态：拒绝所有请求
        HALF_OPEN    // 半开状态：试探性处理请求
    };
    
    // 构造函数
    DatabaseCircuitBreaker(size_t failure_threshold = 10,
                          std::chrono::milliseconds timeout = std::chrono::seconds(30),
                          size_t half_open_success_threshold = 3);
    
    // 检查是否允许请求
    bool allowRequest() const;
    
    // 记录请求结果
    void recordSuccess();
    void recordFailure();
    
    // 获取状态
    State getState() const;
    std::chrono::milliseconds getRemainingTimeout() const;
    size_t getFailureCount() const;
    size_t getSuccessCount() const;
    
    // 重置熔断器
    void reset();
    
    // 获取状态字符串
    std::string getStateString() const;
    
private:
    mutable std::shared_mutex mutex_;
    State state_{State::CLOSED};
    size_t failure_threshold_;
    size_t failure_count_{0};
    size_t success_count_{0};
    size_t half_open_success_threshold_;
    std::chrono::milliseconds timeout_;
    std::chrono::steady_clock::time_point last_failure_time_;
    std::chrono::steady_clock::time_point last_state_change_time_;
    
    // 状态转换
    void transitionTo(State new_state);
    bool shouldTrip() const;
    bool shouldReset() const;
    
    // 时间检查
    bool isTimeoutExpired() const;
};

// ==================== 数据库数据源 ====================
/**
 * @brief 数据库数据源
 * 
 * 从关系型数据库获取数据，支持缓存、重试、熔断器等特性
 */
template<typename T, typename KeyType = std::string>
class DatabaseSource : public IDataSource<T, KeyType> {
public:
    using DataType = T;
    using KeyType = KeyType;
    using RepositoryType = database::IRepository<T>;
    using CacheType = cache::ICache<KeyType, T>;
    
    /**
     * @brief 构造函数
     * @param repository 数据仓储
     * @param config 配置
     */
    explicit DatabaseSource(std::shared_ptr<RepositoryType> repository,
                          const DatabaseSourceConfig& config = {});
    
    /**
     * @brief 析构函数
     */
    ~DatabaseSource() override;
    
    // 禁止拷贝
    DatabaseSource(const DatabaseSource&) = delete;
    DatabaseSource& operator=(const DatabaseSource&) = delete;
    
    // 允许移动
    DatabaseSource(DatabaseSource&&) noexcept = default;
    DatabaseSource& operator=(DatabaseSource&&) noexcept = default;
    
    // =============== IDataSource 接口实现 ===============
    
    /**
     * @brief 获取数据
     */
    std::optional<T> get(const KeyType& key) override;
    
    /**
     * @brief 批量获取数据
     */
    std::unordered_map<KeyType, std::optional<T>> 
    getBatch(const std::vector<KeyType>& keys) override;
    
    /**
     * @brief 检查数据是否存在
     */
    bool contains(const KeyType& key) override;
    
    /**
     * @brief 获取数据源名称
     */
    std::string getName() const override;
    
    /**
     * @brief 获取数据源类型
     */
    DataSourceType getType() const override;
    
    /**
     * @brief 刷新数据源
     */
    void refresh() override;
    
    /**
     * @brief 清理数据源
     */
    void clear() override;
    
    // =============== 高级查询方法 ===============
    
    /**
     * @brief 根据查询构建器查询
     */
    std::vector<T> query(const DatabaseQueryBuilder& builder);
    
    /**
     * @brief 查询单个结果
     */
    std::optional<T> queryOne(const DatabaseQueryBuilder& builder);
    
    /**
     * @brief 查询数量
     */
    size_t queryCount(const DatabaseQueryBuilder& builder);
    
    /**
     * @brief 执行原生SQL查询
     */
    std::vector<T> executeQuery(const std::string& sql, 
                               const std::vector<std::string>& params = {});
    
    /**
     * @brief 执行更新操作
     */
    bool executeUpdate(const std::string& sql, 
                      const std::vector<std::string>& params = {});
    
    // =============== 批量操作方法 ===============
    
    /**
     * @brief 批量保存
     */
    bool saveBatch(const std::vector<T>& entities);
    
    /**
     * @brief 批量更新
     */
    bool updateBatch(const std::vector<T>& entities);
    
    /**
     * @brief 批量删除
     */
    bool deleteBatch(const std::vector<KeyType>& keys);
    
    // =============== 缓存相关方法 ===============
    
    /**
     * @brief 设置查询缓存
     */
    void setQueryCache(std::shared_ptr<CacheType> cache);
    
    /**
     * @brief 清除查询缓存
     */
    void clearQueryCache();
    
    /**
     * @brief 预加载数据到缓存
     */
    bool preloadToCache(const std::vector<KeyType>& keys);
    
    /**
     * @brief 从缓存获取查询结果
     */
    std::optional<std::vector<T>> getQueryFromCache(const DatabaseQueryBuilder& builder);
    
    /**
     * @brief 保存查询结果到缓存
     */
    void saveQueryToCache(const DatabaseQueryBuilder& builder, const std::vector<T>& results);
    
    // =============== 配置管理 ===============
    
    /**
     * @brief 更新配置
     */
    void updateConfig(const DatabaseSourceConfig& config);
    
    /**
     * @brief 获取当前配置
     */
    DatabaseSourceConfig getConfig() const;
    
    // =============== 监控和统计 ===============
    
    /**
     * @brief 获取查询指标
     */
    DatabaseQueryMetrics getMetrics() const;
    
    /**
     * @brief 重置指标
     */
    void resetMetrics();
    
    /**
     * @brief 获取健康状态
     */
    DatabaseHealthChecker::HealthCheckResult getHealthStatus();
    
    /**
     * @brief 获取熔断器状态
     */
    DatabaseCircuitBreaker::State getCircuitBreakerState() const;
    
    /**
     * @brief 生成诊断报告
     */
    std::string generateDiagnosticReport() const;
    
    // =============== 事务支持 ===============
    
    /**
     * @brief 在事务中执行操作
     */
    template<typename Func>
    auto executeInTransaction(Func&& func) -> decltype(func());
    
    /**
     * @brief 开始事务
     */
    bool beginTransaction();
    
    /**
     * @brief 提交事务
     */
    bool commitTransaction();
    
    /**
     * @brief 回滚事务
     */
    bool rollbackTransaction();
    
    // =============== 连接池管理 ===============
    
    /**
     * @brief 获取连接池统计
     */
    struct ConnectionPoolStats {
        size_t active_connections{0};
        size_t idle_connections{0};
        size_t total_connections{0};
        size_t wait_count{0};
        size_t max_wait_time_ms{0};
    };
    
    ConnectionPoolStats getConnectionPoolStats() const;
    
    /**
     * @brief 调整连接池大小
     */
    bool resizeConnectionPool(size_t new_size);
    
private:
    // 内部实现
    struct Impl;
    std::unique_ptr<Impl> impl_;
    
    // 内部方法
    DatabaseQueryResult<T> executeWithRetry(const std::function<std::optional<T>()>& operation,
                                          const std::string& operation_name);
    
    std::vector<DatabaseQueryResult<T>> executeBatchWithRetry(
        const std::function<std::vector<std::optional<T>>()>& operation,
        const std::string& operation_name);
    
    bool checkCircuitBreaker() const;
    void recordQueryMetrics(DatabaseQueryStatus status, 
                           std::chrono::milliseconds execution_time);
    void logSlowQuery(const std::string& query, std::chrono::milliseconds execution_time);
    void logError(const std::string& operation, const std::string& error);
    
    // 缓存管理
    std::optional<T> getFromCache(const KeyType& key);
    void saveToCache(const KeyType& key, const T& value);
    void removeFromCache(const KeyType& key);
    void clearCache();
    
    // 连接管理
    std::shared_ptr<database::IDatabaseConnection> getConnection();
    void releaseConnection(std::shared_ptr<database::IDatabaseConnection> connection);
    bool validateConnection(std::shared_ptr<database::IDatabaseConnection> connection);
    
    // 查询构建辅助
    DatabaseQueryBuilder createQueryBuilder() const;
};

// ==================== 数据库源工厂 ====================
/**
 * @brief 数据库源工厂
 */
class DatabaseSourceFactory {
public:
    // 创建数据库源
    template<typename T, typename KeyType = std::string>
    static std::shared_ptr<DatabaseSource<T, KeyType>> create(
        std::shared_ptr<database::IRepository<T>> repository,
        const DatabaseSourceConfig& config = {}) {
        
        return std::make_shared<DatabaseSource<T, KeyType>>(
            std::move(repository), config);
    }
    
    // 从配置创建
    template<typename T, typename KeyType = std::string>
    static std::shared_ptr<DatabaseSource<T, KeyType>> createFromConfig(
        const std::string& config_path);
    
    // 创建连接池
    static std::shared_ptr<database::IDatabaseConnection> createConnectionPool(
        const DatabaseSourceConfig& config);
};

} // namespace datasource
} // namespace astock

#endif // ASTOCK_INFRASTRUCTURE_DATASOURCE_DATABASESOURCE_H