#include "database/MarketDataRepository.h"
#include "DataSourceRegistry.h"

#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace astock::infrastructure::database {

// ═══ 辅助函数 ═══

static std::string safeStr(const std::string& v) {
    std::string escaped;
    escaped.reserve(v.size() + 2);
    escaped += '\'';
    for (char c : v) {
        if (c == '\'' || c == '\\') escaped += '\\';
        escaped += c;
    }
    escaped += '\'';
    return escaped;
}

static std::string symbolList(const std::vector<std::string>& symbols) {
    std::ostringstream ss;
    ss << '(';
    for (size_t i = 0; i < symbols.size(); ++i) {
        if (i) ss << ',';
        ss << safeStr(symbols[i]);
    }
    ss << ')';
    return ss.str();
}

// ═══ rowToBar ═══

DailyBarRow MarketDataRepository::rowToBar(const astock::database::SqlQueryResultRow& row) {
    DailyBarRow r;
    r.symbol    = row.getString("symbol");
    r.tradeDate = row.getString("trade_date");
    r.open      = row.getDouble("open");
    r.high      = row.getDouble("high");
    r.low       = row.getDouble("low");
    r.close     = row.getDouble("close");
    r.volume    = row.getDouble("volume");
    // turnover 可选
    if (row.contains("turnover")) {
        r.turnover = row.getDouble("turnover");
    }
    return r;
}

// ═══ queryDailyBar ═══

std::vector<DailyBarRow> MarketDataRepository::queryDailyBar(
    const std::string& symbol,
    const std::string& startDate,
    const std::string& endDate,
    const std::vector<std::string>& /*extraFields*/)
{
    std::ostringstream sql;
    sql << "SELECT si.symbol, trade_date, open, high, low, close, volume, turnover"
        << " FROM mkt.daily_bar d JOIN ref.symbol_info si ON d.symbol_id = si.id"
        << " WHERE si.symbol = " << safeStr(symbol)
        << " AND trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " ORDER BY trade_date ASC";

    auto result = db_->executeQuery(sql.str());
    std::vector<DailyBarRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        rows.push_back(rowToBar(result.getRow(i)));
    }
    return rows;
}

// ═══ queryDailyBarBatch ═══

std::vector<DailyBarRow> MarketDataRepository::queryDailyBarBatch(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    if (symbols.empty()) return {};

    std::ostringstream sql;
    sql << "SELECT si.symbol, trade_date, open, high, low, close, volume, turnover"
        << " FROM mkt.daily_bar d JOIN ref.symbol_info si ON d.symbol_id = si.id"
        << " WHERE si.symbol IN " << symbolList(symbols)
        << " AND trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " ORDER BY si.symbol, trade_date ASC";

    auto result = db_->executeQuery(sql.str());
    std::vector<DailyBarRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        rows.push_back(rowToBar(result.getRow(i)));
    }
    return rows;
}

// ═══ queryFieldCrossSection ═══

std::vector<FieldRow> MarketDataRepository::queryFieldCrossSection(
    const std::string& field,
    const std::string& date,
    const std::vector<std::string>& symbols)
{
    std::ostringstream sql;
    sql << "SELECT si.symbol, trade_date, " << field << " AS field_value"
        << " FROM mkt.daily_bar d JOIN ref.symbol_info si ON d.symbol_id = si.id"
        << " WHERE trade_date = " << safeStr(date);

    if (!symbols.empty()) {
        sql << " AND si.symbol IN " << symbolList(symbols);
    }

    auto result = db_->executeQuery(sql.str());
    std::vector<FieldRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        const auto& row = result.getRow(i);
        FieldRow fr;
        fr.symbol    = row.getString("symbol");
        fr.tradeDate = row.getString("trade_date");
        fr.fieldName = field;
        fr.value     = row.getDouble("field_value");
        rows.push_back(fr);
    }
    return rows;
}

// ═══ queryIndexConstituents ═══

std::vector<std::string> MarketDataRepository::queryIndexConstituents(
    const std::string& indexSymbol,
    const std::string& date)
{
    std::ostringstream sql;
    sql << "SELECT constituent_symbol FROM index_constituents"
        << " WHERE index_symbol = " << safeStr(indexSymbol)
        << " AND snapshot_date = " << safeStr(date);

    auto result = db_->executeQuery(sql.str());
    std::vector<std::string> symbols;
    symbols.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        symbols.push_back(result.getRow(i).getString("constituent_symbol"));
    }
    return symbols;
}

// ═══ buildExtraColumnsSql ═══

std::string MarketDataRepository::buildExtraColumnsSql(const std::vector<std::string>& extraFields) const {
    if (extraFields.empty()) return ", turnover";
    std::ostringstream ss;
    ss << ", turnover";
    for (const auto& f : extraFields) {
        // 白名单校验：只允许已知的安全字段名
        static const std::unordered_set<std::string> safe = {
            "pe_ratio","pb_ratio","market_cap","circulating_market_cap",
            "pre_adjust_factor","post_adjust_factor","turnover_rate",
            "change_pct","change_amt","amplitude","industry_code",
            "volume","amount"
        };
        if (safe.count(f)) ss << ", " << f;
    }
    return ss.str();
}

// ═══ queryDailyBarWithFields ═══

std::vector<DailyBarRow> MarketDataRepository::queryDailyBarWithFields(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate,
    const std::vector<std::string>& extraFields)
{
    if (symbols.empty()) return {};
    std::ostringstream sql;
    sql << "SELECT si.symbol, trade_date, open, high, low, close, volume"
        << buildExtraColumnsSql(extraFields)
        << " FROM mkt.daily_bar d JOIN ref.symbol_info si ON d.symbol_id = si.id"
        << " WHERE si.symbol IN " << symbolList(symbols)
        << " AND trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " ORDER BY si.symbol, trade_date ASC";

    auto result = db_->executeQuery(sql.str());
    std::vector<DailyBarRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        rows.push_back(rowToBar(result.getRow(i)));
    }
    return rows;
}

// ═══ queryAllMarketDailyBarWithFields ═══

std::vector<astock::database::SqlQueryResultRow> MarketDataRepository::queryAllMarketDailyBarWithFields(
    const std::string& startDate,
    const std::string& endDate,
    const std::vector<std::string>& extraFields)
{
    std::ostringstream sql;
    sql << "SELECT si.symbol, trade_date, open, high, low, close, volume, turnover"
        << buildExtraColumnsSql(extraFields)
        << " FROM mkt.daily_bar d JOIN ref.symbol_info si ON d.symbol_id = si.id"
        << " WHERE trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " ORDER BY si.symbol, trade_date ASC";

    return db_->executeQuery(sql.str()).getRows();
}

// ═══ queryAllMarketDailyBar ═══

std::vector<DailyBarRow> MarketDataRepository::queryAllMarketDailyBar(
    const std::string& startDate,
    const std::string& endDate)
{
    std::ostringstream sql;
    sql << "SELECT si.symbol, trade_date, open, high, low, close, volume, turnover"
        << " FROM mkt.daily_bar d JOIN ref.symbol_info si ON d.symbol_id = si.id"
        << " WHERE trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " ORDER BY si.symbol, trade_date ASC";

    auto result = db_->executeQuery(sql.str());
    std::vector<DailyBarRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        rows.push_back(rowToBar(result.getRow(i)));
    }
    return rows;
}

// ═══ queryAllMarketWeeklyBar ═══

std::vector<DailyBarRow> MarketDataRepository::queryAllMarketWeeklyBar(
    const std::string& startDate,
    const std::string& endDate)
{
    std::ostringstream sql;
    sql << "SELECT si.symbol, trade_date, open, high, low, close, volume, turnover"
        << " FROM mkt.weekly_bar d JOIN ref.symbol_info si ON d.symbol_id = si.id"
        << " WHERE trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " ORDER BY si.symbol, trade_date ASC";

    auto result = db_->executeQuery(sql.str());
    std::vector<DailyBarRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        rows.push_back(rowToBar(result.getRow(i)));
    }
    return rows;
}

// ═══ queryAllMarketMonthlyBar ═══

std::vector<DailyBarRow> MarketDataRepository::queryAllMarketMonthlyBar(
    const std::string& startDate,
    const std::string& endDate)
{
    std::ostringstream sql;
    sql << "SELECT si.symbol, trade_date, open, high, low, close, volume, turnover"
        << " FROM mkt.monthly_bar d JOIN ref.symbol_info si ON d.symbol_id = si.id"
        << " WHERE trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " ORDER BY si.symbol, trade_date ASC";

    auto result = db_->executeQuery(sql.str());
    std::vector<DailyBarRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        rows.push_back(rowToBar(result.getRow(i)));
    }
    return rows;
}

// ═══ queryAllMarketFinancialData ═══

std::vector<astock::database::SqlQueryResultRow> MarketDataRepository::queryAllMarketFinancialData(
    const std::string& startDate,
    const std::string& endDate)
{
    std::ostringstream sql;
    sql << "SELECT " << cleaning::financial_columns::sqlSelect()
        << " FROM fund.financial_indicator_daily fi"
        << " JOIN ref.symbol_info si ON fi.symbol_id = si.id"
        << " WHERE fi.report_date >= " << safeStr(startDate)
        << " AND fi.report_date <= " << safeStr(endDate)
        << " ORDER BY si.symbol, fi.report_date ASC";

    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        rows.push_back(result.getRow(i));
    }
    return rows;
}

// ═══ queryDailyBarJoined ═══

std::vector<astock::database::SqlQueryResultRow>
MarketDataRepository::queryDailyBarJoined(
    const std::string& startDate,
    const std::string& endDate)
{
    std::ostringstream sql;
    sql << "SELECT " << cleaning::kline_columns::sqlSelect() << ","
        << cleaning::symbol_info_columns::sqlSelect()
        << " FROM mkt.daily_bar d JOIN ref.symbol_info s ON d.symbol_id = s.id LEFT JOIN ref.industry_classification ic ON ic.symbol_id = d.symbol_id AND ic.end_date IS NULL"
        << " WHERE d.trade_date >= " << safeStr(startDate)
        << " AND d.trade_date <= " << safeStr(endDate)
        << " ORDER BY s.symbol, d.trade_date ASC";

    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        rows.push_back(result.getRow(i));
    }
    return rows;
}

std::vector<astock::database::SqlQueryResultRow>
MarketDataRepository::queryDailyBarJoined(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    if (symbols.empty()) return {};
    std::ostringstream sql;
    sql << "SELECT " << cleaning::kline_columns::sqlSelect() << ","
        << cleaning::symbol_info_columns::sqlSelect()
        << " FROM mkt.daily_bar d JOIN ref.symbol_info s ON d.symbol_id = s.id LEFT JOIN ref.industry_classification ic ON ic.symbol_id = d.symbol_id AND ic.end_date IS NULL"
        << " WHERE s.symbol IN " << symbolList(symbols)
        << " AND d.trade_date >= " << safeStr(startDate)
        << " AND d.trade_date <= " << safeStr(endDate)
        << " ORDER BY s.symbol, d.trade_date ASC";

    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        rows.push_back(result.getRow(i));
    }
    return rows;
}

// ═══ queryIndexCodeMap ═══

std::map<std::string, std::string>
MarketDataRepository::queryIndexCodeMap(const std::string& anchorDate)
{
    std::map<std::string, std::string> result;
    std::ostringstream sql;
    sql << "SELECT constituent_symbol, index_symbol"
        << " FROM index_constituents"
        << " WHERE snapshot_date = " << safeStr(anchorDate);
    auto qr = db_->executeQuery(sql.str());
    for (std::size_t i = 0; i < qr.rowCount(); ++i) {
        const auto& row = qr.getRow(i);
        std::string sym = row.getString("constituent_symbol");
        std::string idx = row.getString("index_symbol");
        auto& codes = result[sym];
        if (!codes.empty()) codes += ",";
        codes += idx;
    }
    return result;
}

// ═══ queryIndexList ═══

std::vector<std::string> MarketDataRepository::queryIndexList() {
    std::ostringstream sql;
    sql << "SELECT DISTINCT index_symbol FROM index_constituents ORDER BY index_symbol";
    auto result = db_->executeQuery(sql.str());
    std::vector<std::string> symbols;
    symbols.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        symbols.push_back(result.getRow(i).getString("index_symbol"));
    }
    return symbols;
}

// ═══ queryFinancialData ═══

std::vector<astock::database::SqlQueryResultRow> MarketDataRepository::queryFinancialData(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    if (symbols.empty()) return {};
    std::ostringstream sql;
    sql << "SELECT " << cleaning::financial_columns::sqlSelect()
        << " FROM fund.financial_indicator_daily fi"
        << " JOIN ref.symbol_info si ON fi.symbol_id = si.id"
        << " WHERE si.symbol IN " << symbolList(symbols)
        << " AND fi.report_date >= " << safeStr(startDate)
        << " AND fi.report_date <= " << safeStr(endDate)
        << " ORDER BY si.symbol, fi.report_date ASC";

    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        rows.push_back(result.getRow(i));
    }
    return rows;
}

// ═══ queryWeeklyBar / queryMonthlyBar ═══

std::vector<DailyBarRow> MarketDataRepository::queryWeeklyBar(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    if (symbols.empty()) return {};
    std::ostringstream sql;
    sql << "SELECT si.symbol, trade_date, open, high, low, close, volume, turnover"
        << " FROM mkt.weekly_bar d JOIN ref.symbol_info si ON d.symbol_id = si.id"
        << " WHERE si.symbol IN " << symbolList(symbols)
        << " AND trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " ORDER BY si.symbol, trade_date ASC";
    auto result = db_->executeQuery(sql.str());
    std::vector<DailyBarRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) rows.push_back(rowToBar(result.getRow(i)));
    return rows;
}

std::vector<DailyBarRow> MarketDataRepository::queryMonthlyBar(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    if (symbols.empty()) return {};
    std::ostringstream sql;
    sql << "SELECT si.symbol, trade_date, open, high, low, close, volume, turnover"
        << " FROM mkt.monthly_bar d JOIN ref.symbol_info si ON d.symbol_id = si.id"
        << " WHERE si.symbol IN " << symbolList(symbols)
        << " AND trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " ORDER BY si.symbol, trade_date ASC";
    auto result = db_->executeQuery(sql.str());
    std::vector<DailyBarRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) rows.push_back(rowToBar(result.getRow(i)));
    return rows;
}

// ═══ querySymbolInfo ═══

std::vector<astock::database::SqlQueryResultRow> MarketDataRepository::querySymbolInfo(
    const std::vector<std::string>& symbols)
{
    if (symbols.empty()) return {};
    std::ostringstream sql;
    sql << "SELECT symbol, name, exchange, asset_class, list_date, delist_date, status"
        << " FROM ref.symbol_info"
        << " WHERE symbol IN " << symbolList(symbols);
    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) rows.push_back(result.getRow(i));
    return rows;
}

// ═══ queryKlineDetail ═══

std::vector<astock::database::SqlQueryResultRow>
MarketDataRepository::queryKlineDetail(
    const std::string& symbol,
    const std::string& startDate,
    const std::string& endDate,
    int limit,
    int offset)
{
    std::ostringstream sql;
    sql << "SELECT " << cleaning::kline_columns::sqlSelect() << ","
        << cleaning::symbol_info_columns::sqlSelect()
        << " FROM mkt.daily_bar d JOIN ref.symbol_info s ON d.symbol_id = s.id LEFT JOIN ref.industry_classification ic ON ic.symbol_id = d.symbol_id AND ic.end_date IS NULL"
        << " WHERE s.symbol = " << safeStr(symbol)
        << " AND d.trade_date BETWEEN " << safeStr(startDate) << " AND " << safeStr(endDate)
        << " ORDER BY d.trade_date"
        << " LIMIT " << limit << " OFFSET " << offset;
    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i)
        rows.push_back(result.getRow(i));
    return rows;
}

// ═══ queryFinancialDetail ═══

std::vector<astock::database::SqlQueryResultRow>
MarketDataRepository::queryFinancialDetail(
    const std::string& symbol,
    const std::string& startDate,
    const std::string& endDate,
    int limit,
    int offset)
{
    std::ostringstream sql;
    sql << "SELECT " << cleaning::financial_columns::sqlSelect()
        << " FROM fund.financial_indicator_daily fi JOIN ref.symbol_info si ON fi.symbol_id = si.id"
        << " WHERE si.symbol = " << safeStr(symbol)
        << " AND fi.report_date BETWEEN " << safeStr(startDate) << " AND " << safeStr(endDate)
        << " ORDER BY fi.report_date"
        << " LIMIT " << limit << " OFFSET " << offset;
    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i)
        rows.push_back(result.getRow(i));
    return rows;
}

// ═══ queryActiveSymbols ═══

std::vector<std::string> MarketDataRepository::queryActiveSymbols() {
    auto result = db_->executeQuery(
        "SELECT symbol FROM ref.symbol_info WHERE status = 'ACTIVE' ORDER BY symbol");
    std::vector<std::string> symbols;
    symbols.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i)
        symbols.push_back(result.getRow(i).getString("symbol"));
    return symbols;
}

// ═══ querySymbolCoverage ═══

std::vector<astock::database::SqlQueryResultRow>
MarketDataRepository::querySymbolCoverage(
    const std::string& tableName,
    const std::string& dateColumn,
    const std::string& startDate,
    const std::string& endDate,
    const std::string& joinColumn)
{
    std::ostringstream sql;
    sql << "SELECT si.symbol, MIN(t." << dateColumn << ") AS start_dt,"
        << " MAX(t." << dateColumn << ") AS end_dt, COUNT(*) AS cnt "
        << "FROM " << tableName << " t"
        << " JOIN ref.symbol_info si ON t.symbol_id = si." << joinColumn
        << " WHERE t." << dateColumn << " BETWEEN " << safeStr(startDate)
        << " AND " << safeStr(endDate)
        << " GROUP BY si.symbol";
    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i)
        rows.push_back(result.getRow(i));
    return rows;
}

// ═══ queryPrevTradingDay ═══

std::string MarketDataRepository::queryPrevTradingDay(const std::string& anchorDate) {
    std::ostringstream sql;
    sql << "SELECT MAX(trade_date) FROM trade_calendar"
        << " WHERE trade_date < " << safeStr(anchorDate);
    auto result = db_->executeQuery(sql.str());
    if (result.isEmpty()) return {};
    return result.getRow(0).getString(0);
}

// ═══ isTradingDay ═══

bool MarketDataRepository::isTradingDay(const std::string& date) {
    std::ostringstream sql;
    sql << "SELECT COUNT(*) FROM trade_calendar"
        << " WHERE trade_date = " << safeStr(date) << " AND is_trading_day=1";
    auto result = db_->executeQuery(sql.str());
    if (result.isEmpty()) return false;
    return result.getRow(0).getInt(0) > 0;
}

// ═══ queryTradeCalendar ═══

std::vector<std::string> MarketDataRepository::queryTradeCalendar(
    const std::string& startDate, const std::string& endDate) {
    std::ostringstream sql;
    sql << "SELECT trade_date FROM trade_calendar"
        << " WHERE trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " AND is_trading_day=1"
        << " ORDER BY trade_date";
    auto result = db_->executeQuery(sql.str());
    std::vector<std::string> dates;
    dates.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        dates.push_back(result.getRow(i).getString(0));
    }
    return dates;
}

// ═══ queryNewsSentiment ═══

std::vector<astock::database::SqlQueryResultRow>
MarketDataRepository::queryNewsSentiment(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    if (symbols.empty()) return {};
    std::ostringstream sql;
    sql << "SELECT " << cleaning::news_sentiment_columns::sqlSelect()
        << " FROM data.news_sentiment ns"
        << " JOIN ref.symbol_info si ON ns.symbol_id = si.id"
        << " WHERE si.symbol IN " << symbolList(symbols)
        << " AND ns.publish_time >= " << safeStr(startDate)
        << " AND ns.publish_time <= " << safeStr(endDate)
        << " ORDER BY si.symbol, ns.publish_time ASC";
    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i)
        rows.push_back(result.getRow(i));
    return rows;
}

// ═══ queryAllMarketNewsSentiment ═══

std::vector<astock::database::SqlQueryResultRow>
MarketDataRepository::queryAllMarketNewsSentiment(
    const std::string& startDate,
    const std::string& endDate)
{
    std::ostringstream sql;
    sql << "SELECT " << cleaning::news_sentiment_columns::sqlSelect()
        << " FROM data.news_sentiment ns"
        << " JOIN ref.symbol_info si ON ns.symbol_id = si.id"
        << " WHERE ns.publish_time >= " << safeStr(startDate)
        << " AND ns.publish_time <= " << safeStr(endDate)
        << " ORDER BY si.symbol, ns.publish_time ASC";
    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i)
        rows.push_back(result.getRow(i));
    return rows;
}

// ═══ queryPolicyData ═══

std::vector<astock::database::SqlQueryResultRow>
MarketDataRepository::queryPolicyData(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    if (symbols.empty()) return {};
    std::ostringstream sql;
    sql << "SELECT " << cleaning::policy_data_columns::sqlSelect()
        << " FROM fund.policy_data pd"
        << " JOIN ref.symbol_info si ON pd.symbol_id = si.id"
        << " WHERE si.symbol IN " << symbolList(symbols)
        << " AND pd.publish_time >= " << safeStr(startDate)
        << " AND pd.publish_time <= " << safeStr(endDate)
        << " ORDER BY si.symbol, pd.publish_time ASC";
    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i)
        rows.push_back(result.getRow(i));
    return rows;
}

// ═══ queryAllMarketPolicyData ═══

std::vector<astock::database::SqlQueryResultRow>
MarketDataRepository::queryAllMarketPolicyData(
    const std::string& startDate,
    const std::string& endDate)
{
    std::ostringstream sql;
    sql << "SELECT " << cleaning::policy_data_columns::sqlSelect()
        << " FROM fund.policy_data pd"
        << " JOIN ref.symbol_info si ON pd.symbol_id = si.id"
        << " WHERE pd.publish_time >= " << safeStr(startDate)
        << " AND pd.publish_time <= " << safeStr(endDate)
        << " ORDER BY si.symbol, pd.publish_time ASC";
    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i)
        rows.push_back(result.getRow(i));
    return rows;
}

// ═══ queryAlternativeData ═══

std::vector<astock::database::SqlQueryResultRow>
MarketDataRepository::queryAlternativeData(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    if (symbols.empty()) return {};
    std::ostringstream sql;
    sql << "SELECT " << cleaning::alternative_data_columns::sqlSelect()
        << " FROM fund.alternative_data ad"
        << " JOIN ref.symbol_info si ON ad.symbol_id = si.id"
        << " WHERE si.symbol IN " << symbolList(symbols)
        << " AND ad.trade_date >= " << safeStr(startDate)
        << " AND ad.trade_date <= " << safeStr(endDate)
        << " ORDER BY si.symbol, ad.trade_date ASC";
    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i)
        rows.push_back(result.getRow(i));
    return rows;
}

// ═══ queryAllMarketAlternativeData ═══

std::vector<astock::database::SqlQueryResultRow>
MarketDataRepository::queryAllMarketAlternativeData(
    const std::string& startDate,
    const std::string& endDate)
{
    std::ostringstream sql;
    sql << "SELECT " << cleaning::alternative_data_columns::sqlSelect()
        << " FROM fund.alternative_data ad"
        << " JOIN ref.symbol_info si ON ad.symbol_id = si.id"
        << " WHERE ad.trade_date >= " << safeStr(startDate)
        << " AND ad.trade_date <= " << safeStr(endDate)
        << " ORDER BY si.symbol, ad.trade_date ASC";
    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i)
        rows.push_back(result.getRow(i));
    return rows;
}

// ═══ queryMinuteBar ═══

std::vector<astock::database::SqlQueryResultRow>
MarketDataRepository::queryMinuteBar(
    const std::vector<std::string>& symbols,
    const std::string& startTime,
    const std::string& endTime)
{
    if (symbols.empty()) return {};
    std::ostringstream sql;
    sql << "SELECT " << cleaning::minute_bar_columns::sqlSelect()
        << " FROM mkt.minute_bar mb"
        << " JOIN ref.symbol_info si ON mb.symbol_id = si.id"
        << " WHERE si.symbol IN " << symbolList(symbols)
        << " AND mb.trade_ts >= " << safeStr(startTime)
        << " AND mb.trade_ts <= " << safeStr(endTime)
        << " ORDER BY si.symbol, mb.trade_ts ASC";
    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i)
        rows.push_back(result.getRow(i));
    return rows;
}

// ═══ queryAllMarketMinuteBar ═══

std::vector<astock::database::SqlQueryResultRow>
MarketDataRepository::queryAllMarketMinuteBar(
    const std::string& startTime,
    const std::string& endTime)
{
    std::ostringstream sql;
    sql << "SELECT " << cleaning::minute_bar_columns::sqlSelect()
        << " FROM mkt.minute_bar mb"
        << " JOIN ref.symbol_info si ON mb.symbol_id = si.id"
        << " WHERE mb.trade_ts >= " << safeStr(startTime)
        << " AND mb.trade_ts <= " << safeStr(endTime)
        << " ORDER BY si.symbol, mb.trade_ts ASC";
    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i)
        rows.push_back(result.getRow(i));
    return rows;
}

// ═══ queryCleanedDailyBar ═══

std::vector<astock::database::SqlQueryResultRow>
MarketDataRepository::queryCleanedDailyBar(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    if (symbols.empty()) return {};
    std::ostringstream sql;
    sql << "SELECT " << cleaning::kline_columns::sqlSelect() << ","
        << cleaning::symbol_info_columns::sqlSelect()
        << " FROM data.cleaned_daily_bar d JOIN ref.symbol_info s ON d.symbol_id = s.id"
        << " WHERE s.symbol IN " << symbolList(symbols)
        << " AND d.trade_date >= " << safeStr(startDate)
        << " AND d.trade_date <= " << safeStr(endDate)
        << " ORDER BY s.symbol, d.trade_date ASC";
    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i)
        rows.push_back(result.getRow(i));
    return rows;
}

// ═══ queryNextTradingDay (updated) ═══

std::string MarketDataRepository::queryNextTradingDay(const std::string& anchorDate) {
    std::ostringstream sql;
    sql << "SELECT MIN(trade_date) FROM trade_calendar"
        << " WHERE trade_date > " << safeStr(anchorDate);
    auto result = db_->executeQuery(sql.str());
    if (result.isEmpty()) return {};
    return result.getRow(0).getString(0);
}

} // namespace astock::infrastructure::database