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

    /// 因子字段横截面查询 (mkt.daily_bar, 单日)
    std::vector<FieldRow> queryFieldCrossSection(
        const std::string& field,
        const std::string& date,
        const std::vector<std::string>& symbols = {});

    /// 因子字段范围查询 (mkt.daily_bar, 日期范围，用于首次全量加载)
    std::vector<FieldRow> queryFieldCrossSectionRange(
        const std::string& field,
        const std::string& startDate,
        const std::string& endDate,
        const std::vector<std::string>& symbols);

    /// 财务字段横截面查询 (fund.financial_indicator_daily, 按 report_date ≤ trade_date 取最近一期)
    std::vector<FieldRow> queryFinancialFieldCrossSection(
        const std::string& field,
        const std::string& date,
        const std::vector<std::string>& symbols = {});

    /// 财务字段全部报告期查询 (返回所有 report_date，用于内存缓存)
    std::vector<FieldRow> queryFinancialFieldAllReports(
        const std::string& field,
        const std::string& minReportDate,
        const std::string& maxReportDate,
        const std::vector<std::string>& symbols);

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

    /// 全市场日K线（带额外字段，返回原始行以便访问自定义列）
    std::vector<astock::database::SqlQueryResultRow> queryAllMarketDailyBarWithFields(
        const std::string& startDate,
        const std::string& endDate,
        const std::vector<std::string>& extraFields);

    /// 全市场周K线（按日期范围）
    std::vector<DailyBarRow> queryAllMarketWeeklyBar(
        const std::string& startDate,
        const std::string& endDate);

    /// 全市场月K线（按日期范围）
    std::vector<DailyBarRow> queryAllMarketMonthlyBar(
        const std::string& startDate,
        const std::string& endDate);

    /// 全市场财务数据（按日期范围，显式 25 列）
    std::vector<astock::database::SqlQueryResultRow> queryAllMarketFinancialData(
        const std::string& startDate,
        const std::string& endDate);

    /// 日K线 JOIN symbol_info（返回 26 列：20 K线 + 6 元数据）
    std::vector<astock::database::SqlQueryResultRow> queryDailyBarJoined(
        const std::string& startDate,
        const std::string& endDate);

    /// 日K线 JOIN symbol_info（指定标的列表）
    std::vector<astock::database::SqlQueryResultRow> queryDailyBarJoined(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

    /// 指数成分股映射（symbol → 逗号分隔的 index_code）
    std::map<std::string, std::string> queryIndexCodeMap(
        const std::string& anchorDate);

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

    /// 单标的分页K线详情（含 symbol_info 元数据列，用于 QML 详情面板）
    std::vector<astock::database::SqlQueryResultRow> queryKlineDetail(
        const std::string& symbol,
        const std::string& startDate,
        const std::string& endDate,
        int limit,
        int offset);

    /// 单标的分页财务详情（用于 QML 详情面板）
    std::vector<astock::database::SqlQueryResultRow> queryFinancialDetail(
        const std::string& symbol,
        const std::string& startDate,
        const std::string& endDate,
        int limit,
        int offset);

    /// 新闻舆情查询（按标的+日期范围）
    std::vector<astock::database::SqlQueryResultRow> queryNewsSentiment(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

    /// 全市场新闻舆情
    std::vector<astock::database::SqlQueryResultRow> queryAllMarketNewsSentiment(
        const std::string& startDate,
        const std::string& endDate);

    /// 政策数据查询（按标的+日期范围）
    std::vector<astock::database::SqlQueryResultRow> queryPolicyData(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

    /// 全市场政策数据
    std::vector<astock::database::SqlQueryResultRow> queryAllMarketPolicyData(
        const std::string& startDate,
        const std::string& endDate);

    /// 另类数据查询（按标的+日期范围）
    std::vector<astock::database::SqlQueryResultRow> queryAlternativeData(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

    /// 全市场另类数据
    std::vector<astock::database::SqlQueryResultRow> queryAllMarketAlternativeData(
        const std::string& startDate,
        const std::string& endDate);

    /// 清洗后日线（VIEW: data.cleaned_daily_bar）
    std::vector<astock::database::SqlQueryResultRow> queryCleanedDailyBar(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

    /// 分钟线查询（按标的+时间范围）
    std::vector<astock::database::SqlQueryResultRow> queryMinuteBar(
        const std::vector<std::string>& symbols,
        const std::string& startTime,
        const std::string& endTime);

    /// 全市场分钟线
    std::vector<astock::database::SqlQueryResultRow> queryAllMarketMinuteBar(
        const std::string& startTime,
        const std::string& endTime);

    /// 分钟线日聚合（按 (symbol_id, trade_ts::date) GROUP BY 派生日频列）
    /// 返回列: symbol, trade_date, open_minute, high_minute, low_minute, close_minute, volume_minute
    std::vector<astock::database::SqlQueryResultRow> queryMinuteDailyAgg(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

    /// 获取所有活跃标的列表
    std::vector<std::string> queryActiveSymbols();

    /// 获取标的在日期范围内的覆盖信息（symbol, start_dt, end_dt, cnt）
    /// @param joinColumn ref.symbol_info 的 JOIN 列名（daily_bar 用 "id", financial_indicator 用 "symbol_id"）
    std::vector<astock::database::SqlQueryResultRow> querySymbolCoverage(
        const std::string& tableName,
        const std::string& dateColumn,
        const std::string& startDate,
        const std::string& endDate,
        const std::string& joinColumn = "id");

private:
    static DailyBarRow rowToBar(const astock::database::SqlQueryResultRow& row);
    std::string buildExtraColumnsSql(const std::vector<std::string>& extraFields) const;

    std::shared_ptr<astock::database::ISqlDatabase> db_;
};

} // namespace astock::infrastructure::database