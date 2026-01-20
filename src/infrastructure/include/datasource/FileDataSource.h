/**
 * @file FileDataSource.h
 * @brief 文件数据源实现 - 从CSV/JSON文件读取股票数据
 * 
 * 支持功能：
 * 1. 多种文件格式（CSV、JSON、自定义二进制格式）
 * 2. 增量加载和全量加载
 * 3. 文件监控和自动重载
 * 4. 数据校验和完整性检查
 * 5. 内存映射文件支持（高性能）
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <filesystem>

#include "DataSource.h"
#include "Stock.h"
#include "KLineData.h"
#include "CircuitBreaker.h"
#include "HealthChecker.h"
#include "StatisticsCollector.h"
#include "DataFormat.h"

namespace astock {
namespace datasource {

/**
 * @brief 文件格式类型
 */
enum class FileFormat {
    CSV,        ///< CSV文件格式
    JSON,       ///< JSON文件格式
    BINARY,     ///< 自定义二进制格式
    PARQUET,    ///< Parquet列式存储格式（预留）
    PROTOBUF    ///< Protocol Buffers格式（预留）
};

/**
 * @brief 文件数据源配置
 */
struct FileDataSourceConfig {
    std::filesystem::path file_path;           ///< 文件路径
    FileFormat format = FileFormat::CSV;       ///< 文件格式
    bool memory_mapped = false;                ///< 是否使用内存映射
    bool watch_for_changes = false;            ///< 是否监控文件变化
    std::chrono::seconds reload_interval{30};  ///< 重载间隔
    std::string encoding = "UTF-8";            ///< 文件编码
    char delimiter = ',';                      ///< CSV分隔符
    bool has_header = true;                    ///< 是否有标题行
    
    // 缓存配置
    size_t max_cached_items = 10000;           ///< 最大缓存项数
    std::chrono::minutes cache_ttl{60};        ///< 缓存TTL
    
    // 性能配置
    size_t batch_size = 1000;                  ///< 批量读取大小
    size_t read_buffer_size = 8192;            ///< 读取缓冲区大小
    
    // 错误处理
    size_t max_retries = 3;                    ///< 最大重试次数
    std::chrono::seconds retry_delay{1};       ///< 重试延迟
    bool skip_invalid_lines = true;            ///< 是否跳过无效行
};

/**
 * @brief 文件数据源 - 从文件系统读取股票数据
 * 
 * 支持从CSV/JSON文件加载股票基础信息和K线数据
 * 提供高性能的文件读取和解析能力
 */
class FileDataSource : public IDataSource<Stock, std::string> {
public:
    /**
     * @brief 构造函数
     * @param config 数据源配置
     */
    explicit FileDataSource(const FileDataSourceConfig& config);
    
    /**
     * @brief 析构函数
     */
    ~FileDataSource() override;
    
    // IDataSource接口实现
    std::optional<Stock> get(const std::string& key) override;
    std::vector<Stock> getBatch(const std::vector<std::string>& keys) override;
    std::vector<Stock> getAll() override;
    bool exists(const std::string& key) override;
    size_t count() override;
    bool insert(const Stock& item) override;
    bool update(const Stock& item) override;
    bool remove(const std::string& key) override;
    
    // 文件数据源特有方法
    /**
     * @brief 重新加载文件数据
     */
    void reload();
    
    /**
     * @brief 检查文件是否已更改
     */
    bool hasFileChanged() const;
    
    /**
     * @brief 获取文件统计信息
     */
    std::unordered_map<std::string, size_t> getFileStats() const;
    
    /**
     * @brief 获取文件修改时间
     */
    std::chrono::system_clock::time_point getLastModified() const;
    
    /**
     * @brief 设置文件变更回调
     */
    void setFileChangeCallback(std::function<void(const std::filesystem::path&)> callback);
    
    /**
     * @brief 加载K线数据
     * @param symbol 股票代码
     * @param period K线周期
     * @param start_time 开始时间
     * @param end_time 结束时间
     */
    std::vector<KLineData> loadKLineData(
        const std::string& symbol,
        KLinePeriod period = KLinePeriod::DAY,
        const std::chrono::system_clock::time_point& start_time = std::chrono::system_clock::time_point::min(),
        const std::chrono::system_clock::time_point& end_time = std::chrono::system_clock::time_point::max()
    );
    
    /**
     * @brief 加载股票列表
     * @param market 市场代码（可选）
     */
    std::vector<Stock> loadStockList(const std::string& market = "");
    
    /**
     * @brief 获取数据源统计信息
     */
    DataSourceStats getStats() const override;
    
    /**
     * @brief 获取健康状态
     */
    HealthStatus getHealthStatus() const override;
    
private:
    // 内部实现方法
    void loadFile();
    void loadCSVFile();
    void loadJSONFile();
    void loadBinaryFile();
    
    Stock parseCSVLine(const std::string& line, size_t line_num);
    Stock parseJSONObject(const std::string& json_str);
    Stock parseBinaryRecord(const char* data, size_t size);
    
    void setupFileWatcher();
    void stopFileWatcher();
    void onFileChanged(const std::filesystem::path& path);
    
    void updateCache(const Stock& stock);
    void clearCache();
    std::optional<Stock> getFromCache(const std::string& key);
    
    void validateStock(const Stock& stock) const;
    bool isFileValid() const;
    
    // 成员变量
    FileDataSourceConfig config_;
    std::unordered_map<std::string, Stock> data_map_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> cache_timestamps_;
    
    // 熔断器
    std::unique_ptr<CircuitBreaker> circuit_breaker_;
    
    // 健康检查
    std::unique_ptr<HealthChecker> health_checker_;
    
    // 统计收集器
    std::unique_ptr<StatisticsCollector> stats_collector_;
    
    // 文件监控
    std::unique_ptr<std::thread> file_watcher_thread_;
    std::atomic<bool> file_watcher_running_{false};
    std::filesystem::file_time_type last_modified_;
    std::function<void(const std::filesystem::path&)> file_change_callback_;
    
    // 同步
    mutable std::shared_mutex data_mutex_;
    mutable std::mutex cache_mutex_;
    mutable std::mutex file_mutex_;
    
    // 内存映射（如果需要）
    void* mapped_data_ = nullptr;
    size_t mapped_size_ = 0;
    
    // 内部状态
    std::atomic<bool> is_loaded_{false};
    std::atomic<size_t> load_retries_{0};
    std::chrono::system_clock::time_point last_load_time_;
};

/**
 * @brief 文件数据源构建器（建造者模式）
 */
class FileDataSourceBuilder {
public:
    FileDataSourceBuilder();
    
    FileDataSourceBuilder& withFilePath(const std::filesystem::path& path);
    FileDataSourceBuilder& withFormat(FileFormat format);
    FileDataSourceBuilder& withMemoryMapping(bool enabled);
    FileDataSourceBuilder& withFileWatch(bool enabled);
    FileDataSourceBuilder& withReloadInterval(std::chrono::seconds interval);
    FileDataSourceBuilder& withEncoding(const std::string& encoding);
    FileDataSourceBuilder& withDelimiter(char delimiter);
    FileDataSourceBuilder& withHeader(bool has_header);
    FileDataSourceBuilder& withCacheSize(size_t max_items);
    FileDataSourceBuilder& withCacheTTL(std::chrono::minutes ttl);
    FileDataSourceBuilder& withBatchSize(size_t batch_size);
    FileDataSourceBuilder& withMaxRetries(size_t max_retries);
    FileDataSourceBuilder& withCircuitBreaker(std::unique_ptr<CircuitBreaker> breaker);
    FileDataSourceBuilder& withHealthChecker(std::unique_ptr<HealthChecker> checker);
    FileDataSourceBuilder& withStatsCollector(std::unique_ptr<StatisticsCollector> collector);
    
    std::shared_ptr<FileDataSource> build();
    
private:
    FileDataSourceConfig config_;
    std::unique_ptr<CircuitBreaker> circuit_breaker_;
    std::unique_ptr<HealthChecker> health_checker_;
    std::unique_ptr<StatisticsCollector> stats_collector_;
};

} // namespace datasource
} // namespace astock