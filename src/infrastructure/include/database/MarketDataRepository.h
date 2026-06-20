#pragma once

#include "ISqlDatabase.h"

#include <memory>
#include <string>
#include <vector>

namespace astock::infrastructure::database {

// ═══ 值对象：日K线行 ═══

struct DailyBarRow {
    std::string symbol;
    std::string tradeDate;  // "YYYY-MM-DD"
    double open   = 0.0;
    double high   = 0.0;
    double low    = 0.0;
    double close  = 0.0;
    double volume = 0.0;
    double turnover = 0.0;
};

struct FieldRow {
    std::string symbol;
    std::string tradeDate;
    std::string fieldName;
    double value = 0.0;
};

// ═══ 行情数据仓储：封装所有行情相关 SQL 查询 ═══

class MarketDataRepository {
public:
    explicit MarketDataRepository(std::shared_ptr<astock::database::ISqlDatabase> db)
        : db_(std::move(db)) {}

    /// 日K线查询（单标的）
    std::vector<DailyBarRow> queryDailyBar(
        const std::string& symbol,
        const std::string& startDate,
        const std::string& endDate,
        const std::vector<std::string>& extraFields = {});

    /// 日K线查询（多标的批量）
    std::vector<DailyBarRow> queryDailyBarBatch(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

    /// 因子字段横截面查询
    std::vector<FieldRow> queryFieldCrossSection(
        const std::string& field,
        const std::string& date,
        const std::vector<std::string>& symbols = {});

    /// 指数成分股查询
    std::vector<std::string> queryIndexConstituents(
        const std::string& indexSymbol,
        const std::string& date);

    /// 下一个交易日查询（从 trade_calendar 表，Python sync_trade_calendar.py 同步）
    std::string queryNextTradingDay(const std::string& anchorDate);

    /// 上一个交易日
    std::string queryPrevTradingDay(const std::string& anchorDate);

    /// 检查是否为交易日
    bool isTradingDay(const std::string& date);

    /// 获取日期范围内的交易日列表
    std::vector<std::string> queryTradeCalendar(const std::string& startDate, const std::string& endDate);

    /// 日K线查询（带额外字段：pb_ratio, pe_ratio, market_cap 等）
    std::vector<DailyBarRow> queryDailyBarWithFields(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate,
        const std::vector<std::string>& extraFields);

    /// 全市场日K线（按日期范围）
    std::vector<DailyBarRow> queryAllMarketDailyBar(
        const std::string& startDate,
        const std::string& endDate);

    /// 全市场周K线（按日期范围）
    std::vector<DailyBarRow> queryAllMarketWeeklyBar(
        const std::string& startDate,
        const std::string& endDate);

    /// 全市场月K线（按日期范围）
    std::vector<DailyBarRow> queryAllMarketMonthlyBar(
        const std::string& startDate,
        const std::string& endDate);

    /// 全市场财务数据（按日期范围）
    std::vector<astock::database::SqlQueryResultRow> queryAllMarketFinancialData(
        const std::string& startDate,
        const std::string& endDate);

    /// 指数列表查询
    std::vector<std::string> queryIndexList();

    /// 财务数据查询（按标的+日期范围）
    std::vector<astock::database::SqlQueryResultRow> queryFinancialData(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

    /// 周K线查询
    std::vector<DailyBarRow> queryWeeklyBar(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

    /// 月K线查询
    std::vector<DailyBarRow> queryMonthlyBar(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

    /// 标的元数据查询
    std::vector<astock::database::SqlQueryResultRow> querySymbolInfo(
        const std::vector<std::string>& symbols);

private:
    static DailyBarRow rowToBar(const astock::database::SqlQueryResultRow& row);
    std::string buildExtraColumnsSql(const std::vector<std::string>& extraFields) const;

    std::shared_ptr<astock::database::ISqlDatabase> db_;
};

} // namespace astock::infrastructure::database