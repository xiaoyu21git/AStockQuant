#pragma once
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <map>
#include <chrono>
#include <functional>

// 包含你的实际模型
#include "Bar.h"
#include "Tick.h"

namespace domain::market::repository {

// 前置声明
class IMarketDataRepository;

/**
 * @brief 市场数据仓储接口
 * 基于 domain::model::Bar 和 domain::model::Tick 结构设计
 */
class IMarketDataRepository {
public:
    virtual ~IMarketDataRepository() = default;

    // ============ 类型定义 ============
    using Timestamp = std::int64_t;  // 毫秒级时间戳
    using Bar = domain::model::Bar;
    using Tick = domain::model::Tick;
    
    // 回调函数类型
    using BarsCallback = std::function<void(const std::vector<Bar>&)>;
    using TicksCallback = std::function<void(const std::vector<Tick>&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    // ============ 连接管理 ============
    /**
     * @brief 连接到数据存储
     * @param config_path 配置文件路径（JSON/YAML）
     * @return 连接是否成功
     */
    virtual bool connect(const std::string& config_path = "") = 0;
    
    /**
     * @brief 断开连接
     */
    virtual void disconnect() = 0;
    
    /**
     * @brief 检查连接状态
     */
    virtual bool isConnected() const = 0;

    // ============ Bar数据操作 ============
    /**
     * @brief 保存Bar数据
     * @param symbol 标的代码
     * @param bars Bar数据列表
     * @param replace_existing 是否替换已存在的数据
     * @return 保存是否成功
     */
    virtual bool saveBars(
        const std::string& symbol,
        const std::vector<Bar>& bars,
        bool replace_existing = false) = 0;

    /**
     * @brief 加载Bar数据
     * @param symbol 标的代码
     * @param start_time 开始时间戳（毫秒，0表示不限制）
     * @param end_time 结束时间戳（毫秒，0表示不限制）
     * @param limit 最大返回条数（0表示不限制）
     * @param ascending 是否按时间升序排列
     * @return Bar数据列表
     */
    virtual std::vector<Bar> loadBars(
        const std::string& symbol,
        Timestamp start_time = 0,
        Timestamp end_time = 0,
        size_t limit = 0,
        bool ascending = true) = 0;

    /**
     * @brief 异步加载Bar数据
     */
    virtual void loadBarsAsync(
        const std::string& symbol,
        Timestamp start_time,
        Timestamp end_time,
        const BarsCallback& callback,
        const ErrorCallback& error_callback = nullptr) = 0;

    /**
     * @brief 加载最近N条Bar数据
     */
    virtual std::vector<Bar> loadRecentBars(
        const std::string& symbol,
        size_t count = 1000,
        bool ascending = false) = 0;

    /**
     * @brief 检查Bar数据是否存在
     */
    virtual bool barExists(
        const std::string& symbol,
        Timestamp timestamp) = 0;

    /**
     * @brief 获取Bar数据的时间范围
     * @return pair<最早时间戳, 最晚时间戳>
     */
    virtual std::pair<Timestamp, Timestamp> getBarTimeRange(
        const std::string& symbol) = 0;

    /**
     * @brief 批量保存Bar数据
     */
    virtual void batchSaveBars(
        const std::map<std::string, std::vector<Bar>>& bars_map,
        bool replace_existing = false) = 0;

    /**
     * @brief 删除Bar数据
     */
    virtual size_t deleteBars(
        const std::string& symbol = "",
        Timestamp start_time = 0,
        Timestamp end_time = 0) = 0;

    // ============ Tick数据操作 ============
    /**
     * @brief 保存Tick数据
     */
    virtual bool saveTicks(
        const std::string& symbol,
        const std::vector<Tick>& ticks,
        bool replace_existing = false) = 0;

    /**
     * @brief 加载Tick数据
     */
    virtual std::vector<Tick> loadTicks(
        const std::string& symbol,
        Timestamp start_time = 0,
        Timestamp end_time = 0,
        size_t limit = 0,
        bool ascending = true) = 0;

    /**
     * @brief 异步加载Tick数据
     */
    virtual void loadTicksAsync(
        const std::string& symbol,
        Timestamp start_time,
        Timestamp end_time,
        const TicksCallback& callback,
        const ErrorCallback& error_callback = nullptr) = 0;

    /**
     * @brief 加载最近N条Tick数据
     */
    virtual std::vector<Tick> loadRecentTicks(
        const std::string& symbol,
        size_t count = 10000,
        bool ascending = false) = 0;

    /**
     * @brief 检查Tick数据是否存在
     */
    virtual bool tickExists(
        const std::string& symbol,
        Timestamp timestamp) = 0;

    /**
     * @brief 获取Tick数据的时间范围
     */
    virtual std::pair<Timestamp, Timestamp> getTickTimeRange(
        const std::string& symbol) = 0;

    /**
     * @brief 批量保存Tick数据
     */
    virtual void batchSaveTicks(
        const std::map<std::string, std::vector<Tick>>& ticks_map,
        bool replace_existing = false) = 0;

    /**
     * @brief 删除Tick数据
     */
    virtual size_t deleteTicks(
        const std::string& symbol = "",
        Timestamp start_time = 0,
        Timestamp end_time = 0) = 0;

    // ============ 元数据操作 ============
    /**
     * @brief 标的信息结构
     */
    struct SymbolInfo {
        std::string symbol;
        std::string name;
        std::string exchange;
        std::string type;          // 类型：stock, future, option等
        std::string status;        // 状态：listed, delisted等
        double lot_size;           // 每手数量
        double price_tick;         // 最小价格变动
        std::string other_info;    // JSON格式额外信息
        Timestamp listed_date;     // 上市日期
        Timestamp delisted_date;   // 退市日期
        Timestamp created_at;
        Timestamp updated_at;

        bool isActive() const {
            return status == "listed" || status == "trading";
        }
    };

    /**
     * @brief 保存标的信息
     */
    virtual bool saveSymbolInfo(const SymbolInfo& info) = 0;

    /**
     * @brief 批量保存标的信息
     */
    virtual bool batchSaveSymbolInfo(const std::vector<SymbolInfo>& infos) = 0;

    /**
     * @brief 获取标的信息
     */
    virtual std::optional<SymbolInfo> getSymbolInfo(
        const std::string& symbol) = 0;

    /**
     * @brief 获取所有标的代码
     */
    virtual std::vector<std::string> getAllSymbols(
        const std::string& exchange = "",
        const std::string& type = "") = 0;

    /**
     * @brief 获取所有标的信息
     */
    virtual std::vector<SymbolInfo> getAllSymbolInfos(
        const std::string& exchange = "",
        const std::string& type = "") = 0;

    /**
     * @brief 获取支持的所有交易所
     */
    virtual std::vector<std::string> getAllExchanges() = 0;

    /**
     * @brief 获取支持的所有标的类型
     */
    virtual std::vector<std::string> getAllSymbolTypes() = 0;

    // ============ 数据统计 ============
    struct DataStatistics {
        struct SymbolStats {
            std::string symbol;
            size_t bar_count;
            size_t tick_count;
            Timestamp first_bar_time;
            Timestamp last_bar_time;
            Timestamp first_tick_time;
            Timestamp last_tick_time;
        };

        size_t total_symbols;
        size_t total_bar_records;
        size_t total_tick_records;
        Timestamp overall_first_bar_time;
        Timestamp overall_last_bar_time;
        Timestamp overall_first_tick_time;
        Timestamp overall_last_tick_time;
        std::map<std::string, size_t> bar_counts_by_exchange;
        std::map<std::string, size_t> tick_counts_by_exchange;
        std::vector<SymbolStats> top_symbols_by_bar;  // 前N个标的统计
        std::vector<SymbolStats> top_symbols_by_tick;
    };

    /**
     * @brief 获取数据统计信息
     */
    virtual DataStatistics getStatistics(size_t top_n = 10) const = 0;

    /**
     * @brief 获取标的统计数据
     */
    virtual DataStatistics::SymbolStats getSymbolStatistics(
        const std::string& symbol) const = 0;

    // ============ 数据质量 ============
    struct DataQualityReport {
        struct SymbolQuality {
            std::string symbol;
            size_t total_bars;
            size_t missing_bars;
            size_t invalid_bars;
            size_t duplicate_bars;
            double completeness;  // 数据完整度 0-1
        };

        Timestamp check_time;
        std::vector<SymbolQuality> symbol_qualities;
        double overall_completeness;
        std::vector<std::string> warnings;
        std::vector<std::string> errors;
    };

    /**
     * @brief 检查数据质量
     * @param symbols 要检查的标的（空表示检查所有）
     * @param start_time 开始时间
     * @param end_time 结束时间
     * @param expected_interval 预期数据间隔（毫秒）
     */
    virtual DataQualityReport checkDataQuality(
        const std::vector<std::string>& symbols = {},
        Timestamp start_time = 0,
        Timestamp end_time = 0,
        Timestamp expected_interval = 60000) = 0;

    // ============ 工具方法 ============
    /**
     * @brief 时间戳转换为可读字符串
     */
    static std::string timestampToString(Timestamp ts, bool with_ms = true) {
        auto duration = std::chrono::milliseconds(ts);
        auto tp = std::chrono::system_clock::time_point(duration);
        auto tt = std::chrono::system_clock::to_time_t(tp);
        char buffer[64];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&tt));
        
        if (with_ms) {
            return std::string(buffer) + "." + std::to_string(ts % 1000);
        }
        return std::string(buffer);
    }

    /**
     * @brief 日期字符串转换为时间戳
     */
    static Timestamp dateStringToTimestamp(const std::string& date_str) {
        // 格式: "2024-01-15" 或 "2024-01-15 14:30:00"
        std::tm tm = {};
        std::istringstream ss(date_str);
        
        if (date_str.length() == 10) {  // "YYYY-MM-DD"
            ss >> std::get_time(&tm, "%Y-%m-%d");
        } else if (date_str.length() >= 19) {  // "YYYY-MM-DD HH:MM:SS"
            ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        }
        
        auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch()).count();
    }

    // ============ 事务支持 ============
    virtual void beginTransaction() = 0;
    virtual void commitTransaction() = 0;
    virtual void rollbackTransaction() = 0;
    virtual bool isTransactionActive() const = 0;
    
    /**
     * @brief 自动事务包装器
     */
    template<typename Func>
    auto transaction(Func&& func) -> decltype(func()) {
        beginTransaction();
        try {
            auto result = func();
            commitTransaction();
            return result;
        } catch (...) {
            rollbackTransaction();
            throw;
        }
    }

    // ============ 性能相关 ============
    virtual void enableCache(bool enable = true) = 0;
    virtual void clearCache() = 0;
    virtual size_t getCacheSize() const = 0;
    
    /**
     * @brief 设置批量操作的大小
     */
    virtual void setBatchSize(size_t batch_size) = 0;

    // ============ 工厂方法 ============
    enum class RepositoryType {
        MEMORY,     // 内存存储（测试用）
        SQLITE,     // SQLite数据库
        MYSQL,      // MySQL数据库
        POSTGRESQL, // PostgreSQL数据库
        REDIS,      // Redis缓存
        HYBRID      // 混合存储
    };

    /**
     * @brief 创建仓储实例
     */
    static std::shared_ptr<IMarketDataRepository> create(
        RepositoryType type,
        const std::string& config = "");
};

} // namespace domain::market::repository