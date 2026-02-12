#pragma once

/**
 * @file ThirdPartyApi.h
 * @brief 第三方API统一接口库
 * 
 * 提供统一的接口来调用不同的第三方量化平台API，包括掘金、聚宽等。
 * 支持数据获取、交易执行、账户管理等功能。
 */

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <map>

namespace thirdparty {

// 前置声明
class IThirdPartyApi;
class MarketData;
class AccountInfo;
class PositionInfo;
class OrderInfo;

/**
 * @brief 市场数据类型枚举
 */
enum class MarketDataType {
    TICK,           // 实时Tick数据
    BAR_1M,         // 1分钟K线
    BAR_5M,         // 5分钟K线
    BAR_15M,        // 15分钟K线
    BAR_30M,        // 30分钟K线
    BAR_60M,        // 60分钟K线
    BAR_1D,         // 日K线
    BAR_1W,         // 周K线
    BAR_1MTH        // 月K线
};

/**
 * @brief 订单方向枚举
 */
enum class OrderSide {
    BUY,            // 买入
    SELL            // 卖出
};

/**
 * @brief 订单类型枚举
 */
enum class OrderType {
    LIMIT,          // 限价单
    MARKET,         // 市价单
    STOP,           // 止损单
    STOP_LIMIT      // 止损限价单
};

/**
 * @brief 订单状态枚举
 */
enum class OrderStatus {
    PENDING,        // 待提交
    SUBMITTED,      // 已提交
    PARTIAL_FILLED, // 部分成交
    FILLED,         // 全部成交
    CANCELLED,      // 已取消
    REJECTED        // 已拒绝
};

/**
 * @brief 第三方平台类型枚举
 */
enum class PlatformType {
    JUJIN,          // 掘金
    JUQUAN,         // 聚宽
    RICEQUANT,      // 米筐
    TUSHARE,        // TuShare
    CUSTOM          // 自定义
};

/**
 * @brief 市场数据结构
 */
struct MarketData {
    std::string symbol;             // 标的代码
    double open;                    // 开盘价
    double high;                    // 最高价
    double low;                     // 最低价
    double close;                   // 收盘价
    double volume;                  // 成交量
    double amount;                  // 成交额
    std::chrono::system_clock::time_point timestamp; // 时间戳
    
    MarketData() : open(0), high(0), low(0), close(0), volume(0), amount(0) {}
};

/**
 * @brief 账户信息结构
 */
struct AccountInfo {
    std::string account_id;         // 账户ID
    double total_asset;             // 总资产
    double available_cash;          // 可用资金
    double frozen_cash;             // 冻结资金
    double market_value;            // 持仓市值
    double float_profit;            // 浮动盈亏
    double total_profit;            // 累计盈亏
    
    AccountInfo() : total_asset(0), available_cash(0), frozen_cash(0),
                   market_value(0), float_profit(0), total_profit(0) {}
};

/**
 * @brief 持仓信息结构
 */
struct PositionInfo {
    std::string symbol;             // 标的代码
    double volume;                  // 持仓数量
    double available_volume;        // 可用数量
    double cost_price;              // 成本价
    double market_price;            // 市价
    double market_value;            // 市值
    double float_profit;            // 浮动盈亏
    
    PositionInfo() : volume(0), available_volume(0), cost_price(0),
                    market_price(0), market_value(0), float_profit(0) {}
};

/**
 * @brief 订单信息结构
 */
struct OrderInfo {
    std::string order_id;           // 订单ID
    std::string symbol;             // 标的代码
    OrderSide side;                 // 买卖方向
    OrderType type;                 // 订单类型
    double price;                   // 价格
    double volume;                  // 数量
    double filled_volume;           // 已成交数量
    double filled_amount;           // 已成交金额
    OrderStatus status;             // 订单状态
    std::string status_msg;         // 状态信息
    std::chrono::system_clock::time_point create_time; // 创建时间
    std::chrono::system_clock::time_point update_time; // 更新时间
    
    OrderInfo() : price(0), volume(0), filled_volume(0), filled_amount(0),
                 status(OrderStatus::PENDING) {}
};

/**
 * @brief 配置参数结构
 */
struct ConfigParams {
    PlatformType platform;          // 平台类型
    std::string token;              // API Token
    std::string account_id;         // 账户ID
    std::string server_url;         // 服务器地址
    std::map<std::string, std::string> extra_params; // 额外参数
    
    ConfigParams() : platform(PlatformType::JUJIN) {}
};

/**
 * @brief 回调函数类型定义
 */
using MarketDataCallback = std::function<void(const MarketData&)>;
using OrderCallback = std::function<void(const OrderInfo&)>;
using PositionCallback = std::function<void(const PositionInfo&)>;
using AccountCallback = std::function<void(const AccountInfo&)>;
using ErrorCallback = std::function<void(int, const std::string&)>;

/**
 * @brief 第三方API接口类
 */
class IThirdPartyApi {
public:
    virtual ~IThirdPartyApi() = default;
    
    /**
     * @brief 初始化API
     * @param config 配置参数
     * @return 是否成功
     */
    virtual bool initialize(const ConfigParams& config) = 0;
    
    /**
     * @brief 连接平台
     * @return 是否成功
     */
    virtual bool connect() = 0;
    
    /**
     * @brief 断开连接
     */
    virtual void disconnect() = 0;
    
    /**
     * @brief 获取市场数据
     * @param symbol 标的代码
     * @param data_type 数据类型
     * @param start_time 开始时间
     * @param end_time 结束时间
     * @return 市场数据列表
     */
    virtual std::vector<MarketData> get_market_data(
        const std::string& symbol,
        MarketDataType data_type,
        std::chrono::system_clock::time_point start_time,
        std::chrono::system_clock::time_point end_time) = 0;
    
    /**
     * @brief 获取实时行情
     * @param symbols 标的代码列表
     * @return 实时行情数据
     */
    virtual std::vector<MarketData> get_realtime_quotes(
        const std::vector<std::string>& symbols) = 0;
    
    /**
     * @brief 订阅行情
     * @param symbols 标的代码列表
     * @param data_type 数据类型
     * @param callback 回调函数
     * @return 是否成功
     */
    virtual bool subscribe_market_data(
        const std::vector<std::string>& symbols,
        MarketDataType data_type,
        MarketDataCallback callback) = 0;
    
    /**
     * @brief 取消订阅
     * @param symbols 标的代码列表
     * @param data_type 数据类型
     * @return 是否成功
     */
    virtual bool unsubscribe_market_data(
        const std::vector<std::string>& symbols,
        MarketDataType data_type) = 0;
    
    /**
     * @brief 获取账户信息
     * @return 账户信息
     */
    virtual AccountInfo get_account_info() = 0;
    
    /**
     * @brief 获取持仓信息
     * @return 持仓信息列表
     */
    virtual std::vector<PositionInfo> get_positions() = 0;
    
    /**
     * @brief 获取订单列表
     * @param symbol 标的代码（可选）
     * @param start_time 开始时间（可选）
     * @param end_time 结束时间（可选）
     * @return 订单信息列表
     */
    virtual std::vector<OrderInfo> get_orders(
        const std::string& symbol = "",
        std::chrono::system_clock::time_point start_time = std::chrono::system_clock::time_point(),
        std::chrono::system_clock::time_point end_time = std::chrono::system_clock::time_point()) = 0;
    
    /**
     * @brief 下单
     * @param symbol 标的代码
     * @param side 买卖方向
     * @param type 订单类型
     * @param price 价格
     * @param volume 数量
     * @return 订单ID
     */
    virtual std::string place_order(
        const std::string& symbol,
        OrderSide side,
        OrderType type,
        double price,
        double volume) = 0;
    
    /**
     * @brief 撤单
     * @param order_id 订单ID
     * @return 是否成功
     */
    virtual bool cancel_order(const std::string& order_id) = 0;
    
    /**
     * @brief 设置订单回调
     * @param callback 回调函数
     */
    virtual void set_order_callback(OrderCallback callback) = 0;
    
    /**
     * @brief 设置持仓回调
     * @param callback 回调函数
     */
    virtual void set_position_callback(PositionCallback callback) = 0;
    
    /**
     * @brief 设置账户回调
     * @param callback 回调函数
     */
    virtual void set_account_callback(AccountCallback callback) = 0;
    
    /**
     * @brief 设置错误回调
     * @param callback 回调函数
     */
    virtual void set_error_callback(ErrorCallback callback) = 0;
    
    /**
     * @brief 获取平台类型
     * @return 平台类型
     */
    virtual PlatformType get_platform_type() const = 0;
    
    /**
     * @brief 获取平台名称
     * @return 平台名称
     */
    virtual std::string get_platform_name() const = 0;
    
    /**
     * @brief 检查连接状态
     * @return 是否已连接
     */
    virtual bool is_connected() const = 0;
};

/**
 * @brief 第三方API工厂类
 */
class ThirdPartyApiFactory {
public:
    /**
     * @brief 创建第三方API实例
     * @param platform 平台类型
     * @return API实例指针
     */
    static std::unique_ptr<IThirdPartyApi> create_api(PlatformType platform);
    
    /**
     * @brief 创建掘金API实例
     * @return 掘金API实例指针
     */
    static std::unique_ptr<IThirdPartyApi> create_jujin_api();
    
    /**
     * @brief 创建聚宽API实例
     * @return 聚宽API实例指针
     */
    static std::unique_ptr<IThirdPartyApi> create_juquan_api();
    
    /**
     * @brief 创建米筐API实例
     * @return 米筐API实例指针
     */
    static std::unique_ptr<IThirdPartyApi> create_ricequant_api();
    
    /**
     * @brief 创建TuShare API实例
     * @return TuShare API实例指针
     */
    static std::unique_ptr<IThirdPartyApi> create_tushare_api();
};

/**
 * @brief 工具函数：转换内部代码格式
 * @param internal_symbol 内部代码格式
 * @param platform 目标平台
 * @return 平台代码格式
 */
std::string convert_symbol_format(
    const std::string& internal_symbol,
    PlatformType platform);

/**
 * @brief 工具函数：转换平台代码格式
 * @param platform_symbol 平台代码格式
 * @param platform 源平台
 * @return 内部代码格式
 */
std::string convert_to_internal_format(
    const std::string& platform_symbol,
    PlatformType platform);

} // namespace thirdparty