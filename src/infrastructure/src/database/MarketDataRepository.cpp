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
    sql << "SELECT symbol, trade_date, open, high, low, close, volume, turnover"
        << " FROM daily_bar"
        << " WHERE symbol = " << safeStr(symbol)
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
    sql << "SELECT symbol, trade_date, open, high, low, close, volume, turnover"
        << " FROM daily_bar"
        << " WHERE symbol IN " << symbolList(symbols)
        << " AND trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " ORDER BY symbol, trade_date ASC";

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
    sql << "SELECT symbol, trade_date, " << field << " AS field_value"
        << " FROM daily_bar"
        << " WHERE trade_date = " << safeStr(date);

    if (!symbols.empty()) {
        sql << " AND symbol IN " << symbolList(symbols);
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
    sql << "SELECT symbol, trade_date, open, high, low, close, volume"
        << buildExtraColumnsSql(extraFields)
        << " FROM daily_bar"
        << " WHERE symbol IN " << symbolList(symbols)
        << " AND trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " ORDER BY symbol, trade_date ASC";

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
    sql << "SELECT symbol, trade_date, open, high, low, close, volume, turnover"
        << buildExtraColumnsSql(extraFields)
        << " FROM daily_bar"
        << " WHERE trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " ORDER BY symbol, trade_date ASC";

    return db_->executeQuery(sql.str()).getRows();
}

// ═══ queryAllMarketDailyBar ═══

std::vector<DailyBarRow> MarketDataRepository::queryAllMarketDailyBar(
    const std::string& startDate,
    const std::string& endDate)
{
    std::ostringstream sql;
    sql << "SELECT symbol, trade_date, open, high, low, close, volume, turnover"
        << " FROM daily_bar"
        << " WHERE trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " ORDER BY symbol, trade_date ASC";

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
    sql << "SELECT symbol, trade_date, open, high, low, close, volume, turnover"
        << " FROM weekly_bar"
        << " WHERE trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " ORDER BY symbol, trade_date ASC";

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
    sql << "SELECT symbol, trade_date, open, high, low, close, volume, turnover"
        << " FROM monthly_bar"
        << " WHERE trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " ORDER BY symbol, trade_date ASC";

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
        << " FROM financial_indicator fi"
        << " JOIN symbol_info si ON fi.symbol_id = si.symbol_id"
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
        << " FROM daily_bar d"
        << " JOIN symbol_info s ON d.symbol = s.symbol"
        << " WHERE d.trade_date >= " << safeStr(startDate)
        << " AND d.trade_date <= " << safeStr(endDate)
        << " ORDER BY d.symbol, d.trade_date ASC";

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
        << " FROM daily_bar d"
        << " JOIN symbol_info s ON d.symbol = s.symbol"
        << " WHERE d.symbol IN " << symbolList(symbols)
        << " AND d.trade_date >= " << safeStr(startDate)
        << " AND d.trade_date <= " << safeStr(endDate)
        << " ORDER BY d.symbol, d.trade_date ASC";

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
        << " FROM financial_indicator fi"
        << " JOIN symbol_info si ON fi.symbol_id = si.symbol_id"
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
    sql << "SELECT symbol, trade_date, open, high, low, close, volume, turnover"
        << " FROM weekly_bar"
        << " WHERE symbol IN " << symbolList(symbols)
        << " AND trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " ORDER BY symbol, trade_date ASC";
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
    sql << "SELECT symbol, trade_date, open, high, low, close, volume, turnover"
        << " FROM monthly_bar"
        << " WHERE symbol IN " << symbolList(symbols)
        << " AND trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " ORDER BY symbol, trade_date ASC";
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
        << " FROM symbol_info"
        << " WHERE symbol IN " << symbolList(symbols);
    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) rows.push_back(result.getRow(i));
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