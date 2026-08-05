// MarketDataService.h — 纯 C++ 行情数据服务（零 Qt）
// 使用 ISqlDatabase + MarketDataRepository，返回 foundation::json::JsonFacade
// 替代 DataService 的业务逻辑层
#pragma once
#include "database/MarketDataRepository.h"
#include "foundation/json/json_facade.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace astock::infrastructure::database {

using J = foundation::json::JsonFacade;

// ── 字段名常量（与 DataFieldKeys 对齐） ──
namespace field {
    constexpr const char* SYMBOL      = "symbol";
    constexpr const char* TRADE_DATE  = "trade_date";
    constexpr const char* OPEN        = "open";
    constexpr const char* HIGH        = "high";
    constexpr const char* LOW         = "low";
    constexpr const char* CLOSE       = "close";
    constexpr const char* VOLUME      = "volume";
    constexpr const char* TURNOVER    = "turnover";
    constexpr const char* PRE_CLOSE   = "pre_close";
    constexpr const char* CHANGE_PCT  = "change_pct";
    constexpr const char* CHANGE_AMT  = "change_amt";
    constexpr const char* AMPLITUDE   = "amplitude";
    constexpr const char* TURNOVER_RATE = "turnover_rate";
    constexpr const char* PRE_ADJ_FACTOR  = "pre_adjust_factor";
    constexpr const char* POST_ADJ_FACTOR = "post_adjust_factor";
    constexpr const char* MARKET_CAP  = "market_cap";
    constexpr const char* CIRC_MARKET_CAP = "circulating_market_cap";
    constexpr const char* PE_RATIO    = "pe_ratio";
    constexpr const char* PB_RATIO    = "pb_ratio";
    constexpr const char* INDUSTRY_CODE = "industry_code";
    // 财务
    constexpr const char* REPORT_DATE = "report_date";
    constexpr const char* DISCLOSURE_DATE = "disclosure_date";
    constexpr const char* EPS = "eps";
    constexpr const char* BPS = "bps";
    constexpr const char* ROE = "roe";
    constexpr const char* ROA = "roa";
    constexpr const char* TOTAL_REVENUE = "total_revenue";
    constexpr const char* NET_PROFIT = "net_profit";
    constexpr const char* TOTAL_ASSETS = "total_assets";
    constexpr const char* TOTAL_LIABILITIES = "total_liabilities";
    constexpr const char* EQUITY = "equity";
    constexpr const char* OPERATING_CF = "operating_cash_flow";
    constexpr const char* INVESTING_CF = "investing_cash_flow";
    constexpr const char* FINANCING_CF = "financing_cash_flow";
    constexpr const char* DIVIDEND_YIELD = "dividend_yield";
    // 元数据
    constexpr const char* NAME = "name";
    constexpr const char* EXCHANGE = "exchange";
    constexpr const char* ASSET_CLASS = "asset_class";
    constexpr const char* STATUS = "status";
    constexpr const char* LIST_DATE = "list_date";
    constexpr const char* DELIST_DATE = "delist_date";
}

// ── 查询参数 ──
enum class PriceTableType { Daily, Weekly, Monthly };
enum class DataSourceType { Stock, Index, AllMarket, Financial, SymbolInfo,
    NewsSentiment, PolicyData, AlternativeData, MinuteBar, CleanedDailyBar };

struct MarketDataQuery {
    DataSourceType sourceType{DataSourceType::Stock};
    PriceTableType priceType{PriceTableType::Daily};
    std::vector<std::string> symbols;
    std::string startDate;  // YYYY-MM-DD
    std::string endDate;
    std::vector<std::string> extraFields;
    std::string indexSymbol;  // 指数成分股查询用
    int maxSymbols{500};
};

// ── 查询结果 ──
struct MarketDataResult {
    std::vector<J> rows;
    std::vector<std::string> availableFields;
    int totalRows{0};
    std::string error;
    bool success{false};
};

// ── 纯 C++ 行情数据服务 ──
class MarketDataService {
public:
    using ProgressCallback = std::function<void(int current, int total, const std::string& stage)>;

    explicit MarketDataService(std::shared_ptr<astock::database::ISqlDatabase> db)
        : m_repo(std::make_unique<MarketDataRepository>(std::move(db))) {}

    /// 执行行情查询
    MarketDataResult query(const MarketDataQuery& query, ProgressCallback onProgress = {});

    /// 快捷方法：日K线查询
    MarketDataResult queryDailyBar(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate,
        const std::vector<std::string>& extraFields = {});

    /// 高效批量查询：直接返回 JSON 字符串（跳过逐行 JsonFacade 构建）
    std::string queryDailyBarAsJson(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate,
        const std::vector<std::string>& extraFields = {});

    /// 快捷方法：全市场日K线
    MarketDataResult queryAllMarket(const std::string& startDate, const std::string& endDate);

    /// 快捷方法：指数成分股
    std::vector<std::string> queryIndexConstituents(const std::string& indexSymbol, const std::string& date);

    /// 快捷方法：指数列表
    std::vector<std::string> queryIndexList();

    /// 快捷方法：财务数据
    MarketDataResult queryFinancialData(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

    /// 快捷方法：标的元数据
    MarketDataResult querySymbolInfo(const std::vector<std::string>& symbols);

    /// 下一个交易日
    std::string nextTradingDay(const std::string& anchorDate);

    /// 快捷方法：周K线
    MarketDataResult queryWeeklyBar(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

    /// 快捷方法：月K线
    MarketDataResult queryMonthlyBar(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

    /// 快捷方法：分钟线
    MarketDataResult queryMinuteBar(
        const std::vector<std::string>& symbols,
        const std::string& startTime,
        const std::string& endTime);

    /// 快捷方法：新闻舆情
    MarketDataResult queryNewsSentiment(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

    /// 快捷方法：政策数据
    MarketDataResult queryPolicyData(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

    /// 快捷方法：另类数据
    MarketDataResult queryAlternativeData(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

    /// 快捷方法：清洗后日线
    MarketDataResult queryCleanedDailyBar(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

private:
    static J barRowToJson(const DailyBarRow& row, const std::vector<std::string>& extraFields);
    static J financialRowToJson(const astock::database::SqlQueryResultRow& row);
    static J symbolInfoRowToJson(const astock::database::SqlQueryResultRow& row);
    static J genericRowToJson(const astock::database::SqlQueryResultRow& row);

    std::unique_ptr<MarketDataRepository> m_repo;
};

} // namespace astock::infrastructure::database
