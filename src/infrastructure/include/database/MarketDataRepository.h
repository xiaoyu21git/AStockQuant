#pragma once

#include "ISqlDatabase.h"

#include <map>
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

/// 日K线 + 市值行（策略归因 Brinson 基准行业权重构建用）
struct DailyBarMarketCapRow {
    std::string symbol;
    std::string tradeDate;  // "YYYY-MM-DD"
    double close = 0.0;
    double marketCap = 0.0;              // 总市值
    double circulatingMarketCap = 0.0;   // 流通市值
};

// ═══ 值对象：日终持仓快照（live.daily_position / live.daily_equity_snapshots 入库用） ═══

/// 日终权益快照输入 (live.daily_equity_snapshots)
struct DailyEquitySnapshotInput {
    std::string strategyId;
    std::string tradeDate;    // "YYYY-MM-DD"
    double totalAsset = 0.0;  // 总资产
    double dailyReturn = 0.0; // 相对上一快照的总资产日收益率（无前值 → 0）
};

/// 日终持仓行 (live.daily_position)
struct DailyPositionRow {
    std::string symbol;       // 完整代码 "002601.SZ" 或纯代码 "002601"（入库时按 symbol_info 匹配）
    std::int64_t quantity = 0;
    double costPrice = 0.0;   // avg_cost 摊薄成本
    double marketValue = 0.0;
    double floatingPnl = 0.0;
    double realizedPnl = 0.0; // 引擎不按标的拆已实现盈亏 → 恒 0
};

/// 当前持仓行 (live.current_position 实时同步)
struct CurrentPositionRow {
    std::string symbol;            // 完整代码 "002601.SZ" 或纯代码 "002601"
    std::string strategyId;        // 空 = 手动持仓 (入库为 NULL)
    std::int64_t quantity = 0;
    std::int64_t availableQty = 0; // 可用股数
    std::int64_t frozenQty = 0;    // 冻结股数 (quantity − availableQty)
    double costPrice = 0.0;        // avg_cost 摊薄成本
    double lastPrice = 0.0;
    double marketValue = 0.0;
    double unrealizedPnl = 0.0;
    double pnlPct = 0.0;           // 当前涨幅 % = (lastPrice − costPrice) / costPrice × 100
    double prevClose = 0.0;        // 昨收 (掘金 GM 历史日线回看; 缺 → 0)
    double dayPnl = 0.0;           // 当日盈亏 = (lastPrice − prevClose) × quantity
    double dayPnlPct = 0.0;        // 当日涨跌幅 % = (lastPrice − prevClose) / prevClose × 100
    std::int64_t firstHeldAtEpochSec = 0;  // 持仓开始时间 epoch 秒 (0 → NULL)
    bool removed = false;          // true → 券商已清零, 删行
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

    /// 标的元数据查询（含 industry_code 列）
    std::vector<astock::database::SqlQueryResultRow> querySymbolInfo(
        const std::vector<std::string>& symbols);

    /// 行业名表查询（industry_code → industry_name，仅当前有效行）
    /// 表空时返回空 map，调用方以行业码兜底显示
    std::map<std::string, std::string> queryIndustryNames();

    /// 日K线 + 市值查询（close/market_cap/circulating_market_cap）
    /// 用于 Brinson 基准成分权重构建（流通市值加权）
    std::vector<DailyBarMarketCapRow> queryDailyBarWithMarketCap(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

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

    /// 资金流日频查询（按标的+日期范围，返回 22 列：symbol + trade_date + 20 money_*）
    std::vector<astock::database::SqlQueryResultRow> queryMoneyFlow(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

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

    /// 板块日频聚合（从 mkt.daily_bar 按 industry_code + trade_date GROUP BY）
    /// 返回列: industry_code, trade_date, stock_count, sector_is_reliable,
    ///         sector_breadth, sector_amplitude, sector_turnover,
    ///         sector_return, sector_relative_strength, sector_turnover_ratio
    std::vector<astock::database::SqlQueryResultRow> querySectorDailyAgg(
        const std::string& startDate,
        const std::string& endDate);

    /// 板块资金流日聚合（从 fund.money_flow_daily 按 industry_code + trade_date 汇总）
    /// 返回列: industry_code, trade_date, sector_money_flow_net, sector_money_flow_ratio
    std::vector<astock::database::SqlQueryResultRow> querySectorMoneyFlowAgg(
        const std::string& startDate,
        const std::string& endDate);

    /// 板块集中度（从 mkt.daily_bar 按个股成交额排名计算 Top3 占比）
    /// 返回列: industry_code, trade_date, sector_concentration
    std::vector<astock::database::SqlQueryResultRow> querySectorConcentration(
        const std::string& startDate,
        const std::string& endDate);

    // ═══ live 交易链路: 日终持仓快照持久化 ═══

    /// 日终权益快照 UPSERT (live.daily_equity_snapshots, (strategy_id, trade_date) 唯一)
    /// @param outSnapshotId 输出快照 id（UUID 字符串，daily_position.summary_id 外键引用）
    bool upsertDailyEquitySnapshot(const DailyEquitySnapshotInput& input,
                                   std::string& outSnapshotId);

    /// 上一交易日总资产（日收益率锚点；无前值返回 0）
    double queryPrevDayTotalAsset(const std::string& strategyId,
                                  const std::string& tradeDate);

    /// 日终持仓 UPSERT (live.daily_position, (summary_id, trade_date, symbol_id) 唯一)
    /// @return 成功写入的行数（symbol_info 无匹配的标的跳过）
    int upsertDailyPositions(const std::string& snapshotId,
                             const std::string& tradeDate,
                             const std::vector<DailyPositionRow>& rows);

    /// 当前持仓实时同步 (live.current_position, symbol_id 唯一):
    /// removed=false → UPSERT 整行; removed=true → 删行 (券商已清零)
    /// @return 处理行数
    int syncCurrentPositions(const std::vector<CurrentPositionRow>& rows);

private:
    static DailyBarRow rowToBar(const astock::database::SqlQueryResultRow& row);
    std::string buildExtraColumnsSql(const std::vector<std::string>& extraFields) const;

    std::shared_ptr<astock::database::ISqlDatabase> db_;
};

} // namespace astock::infrastructure::database