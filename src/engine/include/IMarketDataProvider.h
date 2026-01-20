#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <functional>
#include <map>
#include <optional>

namespace engine {

// 基础类型
using Timestamp = std::chrono::system_clock::time_point;
using Duration = std::chrono::nanoseconds;

// 错误码
enum class MarketDataError {
    Success = 0,
    ConnectionFailed,
    DataNotFound,
    InvalidSymbol,
    InvalidTimeRange,
    DataFormatError,
    RateLimitExceeded,
    PermissionDenied,
    UnknownError = 999
};

/**
 * @brief 市场数据粒度
 */
enum class DataGranularity {
    Tick = 0,      // 逐笔数据
    Second1 = 1,   // 1秒
    Second5 = 5,   // 5秒
    Second10 = 10, // 10秒
    Minute1 = 60,  // 1分钟
    Minute5 = 300, // 5分钟
    Minute15 = 900, // 15分钟
    Minute30 = 1800, // 30分钟
    Hour1 = 3600,   // 1小时
    Day1 = 86400,   // 日线
    Week1 = 604800, // 周线
    Month1 = 2592000 // 月线
};

/**
 * @brief 市场数据类型
 */
enum class MarketDataType {
    Stock = 0,      // 股票
    Future = 1,     // 期货
    Option = 2,     // 期权
    Forex = 3,      // 外汇
    Crypto = 4,     // 加密货币
    Index = 5,      // 指数
    Bond = 6,       // 债券
    Fund = 7        // 基金
};

/**
 * @brief 基础行情数据
 */
struct MarketData {
    std::string symbol;           // 标的代码（如：000001.SZ）
    Timestamp timestamp;          // 时间戳
    DataGranularity granularity;  // 数据粒度
    
    // 价格数据
    double open = 0.0;            // 开盘价
    double high = 0.0;            // 最高价
    double low = 0.0;             // 最低价
    double close = 0.0;           // 收盘价
    double pre_close = 0.0;       // 前收盘价
    
    // 成交数据
    double volume = 0.0;          // 成交量（股/手）
    double amount = 0.0;          // 成交额（元）
    int64_t trades = 0;           // 成交笔数
    
    // 盘口数据（Tick级别）
    double ask_price = 0.0;       // 卖一价
    double ask_volume = 0.0;      // 卖一量
    double bid_price = 0.0;       // 买一价
    double bid_volume = 0.0;      // 买一量
    
    // 衍生数据
    double avg_price = 0.0;       // 均价
    double vwap = 0.0;            // 成交量加权平均价
    double turnover_rate = 0.0;   // 换手率
    
    // 扩展字段
    std::map<std::string, double> extra_fields; // 扩展字段
    std::vector<double> bid_prices;  // 买一至买五价
    std::vector<double> bid_volumes; // 买一至买五量
    std::vector<double> ask_prices;  // 卖一至卖五价
    std::vector<double> ask_volumes; // 卖一至卖五量
    
    // 验证数据有效性
    bool is_valid() const;
    
    // 转换为字符串（用于调试）
    std::string to_string() const;
};

/**
 * @brief K线数据
 */
struct KLineData {
    std::string symbol;
    Timestamp timestamp;
    DataGranularity granularity;
    
    // OHLCV 数据
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
    double amount = 0.0;
    
    // 其他指标
    double ma5 = 0.0;     // 5日均线
    double ma10 = 0.0;    // 10日均线
    double ma20 = 0.0;    // 20日均线
    double ma60 = 0.0;    // 60日均线
    
    double upper_band = 0.0; // 布林上轨
    double middle_band = 0.0; // 布林中轨
    double lower_band = 0.0; // 布林下轨
    
    // 成交量指标
    double vol_ma5 = 0.0;   // 5日成交量均线
    double vol_ma10 = 0.0;  // 10日成交量均线
    
    bool is_valid() const;
};

/**
 * @brief 分时数据
 */
struct TimeShareData {
    std::string symbol;
    Timestamp timestamp;
    double price = 0.0;        // 当前价格
    double avg_price = 0.0;    // 均价
    double volume = 0.0;       // 当前成交量
    double amount = 0.0;       // 当前成交额
    
    // 买卖盘口
    std::vector<double> bid_prices;   // 买盘价格
    std::vector<double> bid_volumes;  // 买盘数量
    std::vector<double> ask_prices;   // 卖盘价格
    std::vector<double> ask_volumes;  // 卖盘数量
};

/**
 * @brief 标的元数据
 */
struct SymbolInfo {
    std::string symbol;           // 标的代码
    std::string name;             // 标的名称
    MarketDataType type;          // 标的类型
    
    // 交易信息
    double price_precision = 0.01;    // 价格精度
    double volume_precision = 1.0;    // 数量精度
    double min_trade_volume = 100.0;  // 最小交易单位
    double max_trade_volume = 1000000.0; // 最大交易单位
    
    // 时间信息
    std::vector<std::pair<Timestamp, Timestamp>> trade_sessions; // 交易时段
    std::vector<Timestamp> holidays;   // 节假日
    
    // 扩展信息
    std::string market;          // 市场（SZ、SH、HK等）
    std::string industry;        // 行业
    std::string sector;          // 板块
    std::map<std::string, std::string> extra_info; // 扩展信息
};

/**
 * @brief 数据订阅选项
 */
struct SubscribeOptions {
    std::vector<std::string> symbols;      // 订阅的标的
    DataGranularity granularity = DataGranularity::Minute1; // 数据粒度
    
    // 过滤选项
    bool include_extended_hours = false;   // 是否包含盘前盘后
    bool include_auction = true;           // 是否包含集合竞价
    bool include_canceled = false;         // 是否包含已撤销订单
    
    // 数据字段选项
    bool include_order_book = false;       // 是否包含订单簿
    bool include_trade_detail = false;     // 是否包含成交明细
    bool include_statistics = false;       // 是否包含统计信息
    
    // 回调选项
    uint32_t max_queue_size = 1000;        // 最大回调队列大小
    bool enable_batch_callback = false;    // 是否启用批量回调
    uint32_t batch_size = 100;             // 批量大小
};

/**
 * @brief 市场数据回调接口
 */
class IMarketDataCallback {
public:
    virtual ~IMarketDataCallback() = default;
    
    /**
     * @brief 实时行情数据回调
     * @param data 行情数据
     */
    virtual void on_market_data(const MarketData& data) = 0;
    
    /**
     * @brief K线数据回调
     * @param data K线数据
     */
    virtual void on_kline_data(const KLineData& data) = 0;
    
    /**
     * @brief 分时数据回调
     * @param data 分时数据
     */
    virtual void on_timeshare_data(const TimeShareData& data) = 0;
    
    /**
     * @brief 批量数据回调
     * @param data_list 数据列表
     */
    virtual void on_batch_data(const std::vector<MarketData>& data_list) = 0;
    
    /**
     * @brief 错误回调
     * @param error 错误码
     * @param message 错误信息
     */
    virtual void on_error(MarketDataError error, const std::string& message) = 0;
    
    /**
     * @brief 连接状态变化回调
     * @param connected 是否已连接
     * @param message 状态信息
     */
    virtual void on_connection_status(bool connected, const std::string& message) = 0;
    
    /**
     * @brief 订阅状态变化回调
     * @param symbol 标的代码
     * @param subscribed 是否已订阅
     */
    virtual void on_subscription_status(const std::string& symbol, bool subscribed) = 0;
};

/**
 * @brief 市场数据提供器接口
 */
class IMarketDataProvider {
public:
    virtual ~IMarketDataProvider() = default;
    
    // --- 连接管理 ---
    
    /**
     * @brief 连接到数据源
     * @param config 连接配置
     * @return 连接是否成功
     */
    virtual bool connect(const std::map<std::string, std::string>& config) = 0;
    
    /**
     * @brief 断开连接
     */
    virtual void disconnect() = 0;
    
    /**
     * @brief 检查连接状态
     * @return 是否已连接
     */
    virtual bool is_connected() const = 0;
    
    // --- 数据订阅 ---
    
    /**
     * @brief 订阅实时行情
     * @param options 订阅选项
     * @return 是否订阅成功
     */
    virtual bool subscribe(const SubscribeOptions& options) = 0;
    
    /**
     * @brief 取消订阅
     * @param symbols 要取消订阅的标的列表，为空则取消所有
     * @return 是否成功
     */
    virtual bool unsubscribe(const std::vector<std::string>& symbols = {}) = 0;
    
    /**
     * @brief 获取已订阅的标的列表
     * @return 标的列表
     */
    virtual std::vector<std::string> get_subscribed_symbols() const = 0;
    
    // --- 历史数据获取 ---
    
    /**
     * @brief 获取历史K线数据
     * @param symbol 标的代码
     * @param granularity 数据粒度
     * @param start_time 开始时间
     * @param end_time 结束时间
     * @param limit 最大数据条数
     * @return K线数据列表
     */
    virtual std::vector<KLineData> get_historical_klines(
        const std::string& symbol,
        DataGranularity granularity,
        Timestamp start_time,
        Timestamp end_time,
        uint32_t limit = 1000) = 0;
    
    /**
     * @brief 获取历史Tick数据
     * @param symbol 标的代码
     * @param date 日期
     * @param limit 最大数据条数
     * @return Tick数据列表
     */
    virtual std::vector<MarketData> get_historical_ticks(
        const std::string& symbol,
        Timestamp date,
        uint32_t limit = 10000) = 0;
    
    /**
     * @brief 获取日线数据
     * @param symbol 标的代码
     * @param start_date 开始日期
     * @param end_date 结束日期
     * @return 日线数据列表
     */
    virtual std::vector<KLineData> get_daily_data(
        const std::string& symbol,
        Timestamp start_date,
        Timestamp end_date) = 0;
    
    // --- 实时数据获取 ---
    
    /**
     * @brief 获取最新行情
     * @param symbol 标的代码
     * @return 最新行情数据，如果不存在则返回nullopt
     */
    virtual std::optional<MarketData> get_latest_quote(const std::string& symbol) = 0;
    
    /**
     * @brief 获取最新K线
     * @param symbol 标的代码
     * @param granularity K线粒度
     * @return 最新K线数据
     */
    virtual std::optional<KLineData> get_latest_kline(
        const std::string& symbol,
        DataGranularity granularity) = 0;
    
    /**
     * @brief 获取当前分时数据
     * @param symbol 标的代码
     * @return 分时数据
     */
    virtual std::optional<TimeShareData> get_current_timeshare(const std::string& symbol) = 0;
    
    // --- 元数据获取 ---
    
    /**
     * @brief 获取标的元数据
     * @param symbol 标的代码
     * @return 标的信息，如果不存在则返回nullopt
     */
    virtual std::optional<SymbolInfo> get_symbol_info(const std::string& symbol) = 0;
    
    /**
     * @brief 获取所有可用的标的列表
     * @param market 市场过滤（可选）
     * @param type 类型过滤（可选）
     * @return 标的列表
     */
    virtual std::vector<std::string> get_available_symbols(
        const std::string& market = "",
        MarketDataType type = MarketDataType::Stock) = 0;
    
    /**
     * @brief 搜索标的
     * @param keyword 关键词（代码或名称）
     * @param limit 最大返回数量
     * @return 搜索结果
     */
    virtual std::vector<SymbolInfo> search_symbols(
        const std::string& keyword,
        uint32_t limit = 10) = 0;
    
    // --- 回调管理 ---
    
    /**
     * @brief 注册数据回调
     * @param callback 回调接口
     */
    virtual void register_callback(IMarketDataCallback* callback) = 0;
    
    /**
     * @brief 取消注册回调
     * @param callback 回调接口
     */
    virtual void unregister_callback(IMarketDataCallback* callback) = 0;
    
    // --- 配置和管理 ---
    
    /**
     * @brief 获取数据提供器名称
     * @return 提供器名称
     */
    virtual std::string get_name() const = 0;
    
    /**
     * @brief 获取数据提供器类型
     * @return 提供器类型
     */
    virtual std::string get_type() const = 0;
    
    /**
     * @brief 获取支持的粒度列表
     * @return 支持的粒度
     */
    virtual std::vector<DataGranularity> get_supported_granularities() const = 0;
    
    /**
     * @brief 获取支持的市场类型
     * @return 支持的市场类型
     */
    virtual std::vector<MarketDataType> get_supported_markets() const = 0;
    
    /**
     * @brief 检查是否支持某个标的
     * @param symbol 标的代码
     * @return 是否支持
     */
    virtual bool is_symbol_supported(const std::string& symbol) const = 0;
    
    /**
     * @brief 检查是否支持某个粒度
     * @param granularity 数据粒度
     * @return 是否支持
     */
    virtual bool is_granularity_supported(DataGranularity granularity) const = 0;
    
    /**
     * @brief 获取数据延迟（毫秒）
     * @return 数据延迟
     */
    virtual Duration get_data_latency() const = 0;
    
    /**
     * @brief 获取最后更新时间
     * @return 最后更新时间
     */
    virtual Timestamp get_last_update_time() const = 0;
    
    /**
     * @brief 获取统计数据
     * @return 统计信息
     */
    struct Statistics {
        uint64_t total_data_points = 0;
        uint64_t total_requests = 0;
        uint64_t failed_requests = 0;
        Duration avg_latency;
        Timestamp start_time;
    };
    virtual Statistics get_statistics() const = 0;
    
    /**
     * @brief 重置统计信息
     */
    virtual void reset_statistics() = 0;
};

/**
 * @brief 市场数据提供器工厂
 */
class MarketDataProviderFactory {
public:
    virtual ~MarketDataProviderFactory() = default;
    
    /**
     * @brief 创建数据提供器
     * @param type 提供器类型
     * @param config 配置参数
     * @return 数据提供器指针
     */
    virtual std::unique_ptr<IMarketDataProvider> create_provider(
        const std::string& type,
        const std::map<std::string, std::string>& config) = 0;
    
    /**
     * @brief 获取支持的数据提供器类型列表
     * @return 类型列表
     */
    virtual std::vector<std::string> get_supported_types() const = 0;
    
    /**
     * @brief 注册自定义数据提供器
     * @param type 类型名称
     * @param creator 创建函数
     */
    virtual void register_provider_type(
        const std::string& type,
        std::function<std::unique_ptr<IMarketDataProvider>(
            const std::map<std::string, std::string>& config)> creator) = 0;
    
    /**
     * @brief 获取全局工厂实例
     * @return 工厂实例
     */
    static MarketDataProviderFactory& instance();
};

} // namespace engine