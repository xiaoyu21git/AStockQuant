#ifndef ASTOCK_INFRASTRUCTURE_DATABASE_MARKETDATAMODELS_H
#define ASTOCK_INFRASTRUCTURE_DATABASE_MARKETDATAMODELS_H

#include <string>
#include <chrono>
#include <ctime>

namespace astock {
namespace database {

/**
 * @brief 标的类型
 */
enum class SymbolType {
    STOCK,    // 股票
    FUTURE,   // 期货
    ETF,      // ETF
    INDEX     // 指数
};

/**
 * @brief 标的信息
 */
struct SymbolInfo {
    std::string symbol;           // 标的代码
    std::string name;             // 标的名称
    SymbolType symbol_type;       // 标的类型
    std::string exchange;         // 交易所
    std::time_t list_date;        // 上市日期
    std::time_t delist_date;      // 退市日期
    std::string status;           // 状态: active/delisted
    std::time_t created_at;       // 创建时间
    std::time_t updated_at;       // 更新时间
    
    SymbolInfo() : symbol_type(SymbolType::STOCK), 
                   list_date(0), delist_date(0),
                   created_at(std::time(nullptr)),
                   updated_at(std::time(nullptr)),
                   status("active") {}
};

/**
 * @brief 日线数据
 */
struct DailyBar {
    int id;                      // 主键ID
    std::string symbol;          // 标的代码
    std::time_t trade_date;      // 交易日期
    double open;                 // 开盘价
    double high;                 // 最高价
    double low;                  // 最低价
    double close;                // 收盘价
    double pre_close;            // 前收盘价
    double volume;               // 成交量
    double turnover;             // 成交额
    double change_pct;           // 涨跌幅%
    double amplitude;            // 振幅%
    double turnover_rate;        // 换手率%
    double pe_ratio;             // 市盈率
    double pb_ratio;             // 市净率
    double market_cap;           // 总市值
    std::time_t created_at;      // 创建时间
    
    DailyBar() : id(0), trade_date(0), 
                 open(0), high(0), low(0), close(0),
                 pre_close(0), volume(0), turnover(0),
                 change_pct(0), amplitude(0), turnover_rate(0),
                 pe_ratio(0), pb_ratio(0), market_cap(0),
                 created_at(std::time(nullptr)) {}
};

/**
 * @brief 分钟线数据
 */
struct MinuteBar {
    int id;                      // 主键ID
    std::string symbol;          // 标的代码
    std::time_t datetime;        // 时间
    int frequency;               // 频率: 1/5/15/30/60分钟
    double open;                 // 开盘价
    double high;                 // 最高价
    double low;                  // 最低价
    double close;                // 收盘价
    double volume;               // 成交量
    double turnover;             // 成交额
    std::time_t created_at;      // 创建时间
    
    MinuteBar() : id(0), datetime(0), frequency(1),
                  open(0), high(0), low(0), close(0),
                  volume(0), turnover(0),
                  created_at(std::time(nullptr)) {}
};

/**
 * @brief Tick数据
 */
struct TickData {
    int id;                      // 主键ID
    std::string symbol;          // 标的代码
    std::time_t datetime;        // 时间(精确到毫秒)
    double last_price;           // 最新价
    double volume;               // 成交量
    double turnover;             // 成交额
    double bid_price;            // 买一价
    double bid_volume;           // 买一量
    double ask_price;            // 卖一价
    double ask_volume;           // 卖一量
    std::time_t created_at;      // 创建时间
    
    TickData() : id(0), datetime(0),
                 last_price(0), volume(0), turnover(0),
                 bid_price(0), bid_volume(0),
                 ask_price(0), ask_volume(0),
                 created_at(std::time(nullptr)) {}
};

/**
 * @brief 日度资金流向数据
 */
struct MoneyFlowDaily {
    int id;                      // 主键ID
    std::string symbol;          // 标的代码
    std::time_t trade_date;      // 交易日期
    double main_inflow;          // 主力流入金额
    double main_outflow;         // 主力流出金额
    double net_main_inflow;      // 主力净流入
    double large_inflow;         // 大单流入金额
    double large_outflow;        // 大单流出金额
    double medium_inflow;        // 中单流入金额
    double medium_outflow;       // 中单流出金额
    double small_inflow;         // 小单流入金额
    double small_outflow;        // 小单流出金额
    double net_amount;           // 总净流入金额
    std::time_t created_at;      // 创建时间

    MoneyFlowDaily()
        : id(0), trade_date(0),
          main_inflow(0), main_outflow(0), net_main_inflow(0),
          large_inflow(0), large_outflow(0),
          medium_inflow(0), medium_outflow(0),
          small_inflow(0), small_outflow(0),
          net_amount(0),
          created_at(std::time(nullptr)) {}
};

/**
 * @brief 龙虎榜日度上榜记录
 */
struct DragonTigerRecord {
    int id;                      // 主键ID
    std::string symbol;          // 标的代码
    std::time_t trade_date;      // 交易日期
    std::string reason;          // 上榜原因
    double buy_amount;           // 买入金额
    double sell_amount;          // 卖出金额
    double net_amount;           // 净买入金额
    unsigned int buy_count;      // 买入席位数量
    unsigned int sell_count;     // 卖出席位数量
    double institution_buy;      // 机构净买入
    double institution_sell;     // 机构净卖出
    double turnover_rate;        // 当日换手率
    std::time_t created_at;      // 创建时间

    DragonTigerRecord()
        : id(0), trade_date(0),
          buy_amount(0), sell_amount(0), net_amount(0),
          buy_count(0), sell_count(0),
          institution_buy(0), institution_sell(0),
          turnover_rate(0),
          created_at(std::time(nullptr)) {}
};

/**
 * @brief 时间辅助函数
 */
inline std::time_t dateToTimestamp(int year, int month, int day) {
    std::tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    return std::mktime(&tm);
}

inline std::string timestampToString(std::time_t timestamp, const char* format = "%Y-%m-%d") {
    char buffer[80];
    std::tm* tm_info = std::localtime(&timestamp);
    std::strftime(buffer, sizeof(buffer), format, tm_info);
    return std::string(buffer);
}

inline std::string symbolTypeToString(SymbolType type) {
    switch(type) {
        // 与数据库 symbol_info.asset_class ENUM 对齐（大写），
        // 同时兼容原有小写用法
        case SymbolType::STOCK: return "STOCK";
        case SymbolType::FUTURE: return "FUTURE";
        case SymbolType::ETF: return "ETF";
        case SymbolType::INDEX: return "INDEX";
        default: return "unknown";
    }
}

inline SymbolType stringToSymbolType(const std::string& str) {
    // 兼容大小写：DB 中是大写 ENUM，历史代码可能用小写
    if (str == "stock" || str == "STOCK") return SymbolType::STOCK;
    if (str == "future" || str == "FUTURE") return SymbolType::FUTURE;
    if (str == "etf" || str == "ETF") return SymbolType::ETF;
    if (str == "index" || str == "INDEX") return SymbolType::INDEX;
    return SymbolType::STOCK;
}

} // namespace database
} // namespace astock

#endif // ASTOCK_INFRASTRUCTURE_DATABASE_MARKETDATAMODELS_H
