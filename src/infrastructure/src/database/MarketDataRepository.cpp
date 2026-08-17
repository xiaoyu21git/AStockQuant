#include "database/MarketDataRepository.h"
#include "database/SqlEscape.h"
#include "DataSourceRegistry.h"
#include "foundation/log/logging.hpp"
#include "foundation/market/AStockSymbol.h"

#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace astock::infrastructure::database {

// ═══ 辅助函数 ═══

using astock::database::safeStr;

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
    sql << "SELECT constituent_symbol FROM ref.index_constituents"
        << " WHERE index_symbol = " << safeStr(indexSymbol)
        << " AND start_date <= " << safeStr(date)
        << " AND (end_date IS NULL OR end_date >= " << safeStr(date) << ")";

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
        << " FROM mkt.daily_bar d JOIN ref.symbol_info s ON d.symbol_id = s.id"
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
        << " FROM mkt.daily_bar d JOIN ref.symbol_info s ON d.symbol_id = s.id"
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

// ═══ queryFieldCrossSectionRange ═══

std::vector<FieldRow> MarketDataRepository::queryFieldCrossSectionRange(
    const std::string& field,
    const std::string& startDate,
    const std::string& endDate,
    const std::vector<std::string>& symbols)
{
    std::ostringstream sql;
    sql << "SELECT si.symbol, trade_date, " << field << " AS field_value"
        << " FROM mkt.daily_bar d JOIN ref.symbol_info si ON d.symbol_id = si.id"
        << " WHERE trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate);
    if (!symbols.empty())
        sql << " AND si.symbol IN " << symbolList(symbols);
    sql << " ORDER BY si.symbol, trade_date ASC";

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

// ═══ queryFinancialFieldCrossSection ═══

std::vector<FieldRow> MarketDataRepository::queryFinancialFieldCrossSection(
    const std::string& field,
    const std::string& date,
    const std::vector<std::string>& symbols)
{
    std::vector<FieldRow> rows;
    if (symbols.empty()) return rows;

    std::ostringstream sql;
    sql << "SELECT si.symbol, fi.report_date AS trade_date, fi." << field << " AS field_value"
        << " FROM fund.financial_indicator_daily fi"
        << " JOIN ref.symbol_info si ON fi.symbol_id = si.id"
        << " WHERE si.symbol IN " << symbolList(symbols)
        << " AND fi.report_date <= " << safeStr(date)
        << " ORDER BY si.symbol, fi.report_date DESC";

    auto result = db_->executeQuery(sql.str());
    // 每组(symbol)取 report_date 最近的一条（已按 DESC 排序，取第一条）
    std::string lastSym;
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        const auto& row = result.getRow(i);
        std::string sym = row.getString("symbol");
        if (sym == lastSym) continue; // 跳过同一 symbol 的更早 report_date
        lastSym = sym;
        FieldRow fr;
        fr.symbol    = sym;
        fr.tradeDate = row.getString("trade_date");
        fr.fieldName = field;
        fr.value     = row.getDouble("field_value");
        rows.push_back(fr);
    }
    return rows;
}

// ═══ queryFinancialFieldAllReports ═══

std::vector<FieldRow> MarketDataRepository::queryFinancialFieldAllReports(
    const std::string& field,
    const std::string& minReportDate,
    const std::string& maxReportDate,
    const std::vector<std::string>& symbols)
{
    std::vector<FieldRow> rows;
    std::ostringstream sql;
    sql << "SELECT si.symbol, fi.report_date AS trade_date, fi." << field << " AS field_value"
        << " FROM fund.financial_indicator_daily fi"
        << " JOIN ref.symbol_info si ON fi.symbol_id = si.id"
        << " WHERE fi.report_date >= " << safeStr(minReportDate)
        << " AND fi.report_date <= " << safeStr(maxReportDate);
    if (!symbols.empty())
        sql << " AND si.symbol IN " << symbolList(symbols);
    sql << " ORDER BY si.symbol, fi.report_date ASC";

    auto result = db_->executeQuery(sql.str());
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        const auto& r = result.getRow(i);
        FieldRow fr;
        fr.symbol    = r.getString("symbol");
        fr.tradeDate = r.getString("trade_date");
        fr.fieldName = field;
        fr.value     = r.getDouble("field_value");
        rows.push_back(fr);
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
        << " WHERE start_date <= " << safeStr(anchorDate)
        << " AND (end_date IS NULL OR end_date >= " << safeStr(anchorDate) << ")";
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
    sql << "SELECT symbol, name, exchange, asset_class, list_date, delist_date, status, industry_code"
        << " FROM ref.symbol_info"
        << " WHERE symbol IN " << symbolList(symbols);
    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) rows.push_back(result.getRow(i));
    return rows;
}

// ═══ queryIndustryNames ═══

std::map<std::string, std::string> MarketDataRepository::queryIndustryNames()
{
    std::map<std::string, std::string> result;
    const std::string sql =
        "SELECT DISTINCT industry_code, industry_name FROM ref.industry_classification"
        " WHERE end_date IS NULL AND industry_code IS NOT NULL AND industry_name IS NOT NULL";
    auto qr = db_->executeQuery(sql);
    for (std::size_t i = 0; i < qr.rowCount(); ++i) {
        const auto& row = qr.getRow(i);
        std::string code = row.getString("industry_code");
        std::string name = row.getString("industry_name");
        if (code.empty() || name.empty()) continue;
        result[std::move(code)] = std::move(name);  // 同码异名取最后一行
    }
    return result;
}

// ═══ queryDailyBarWithMarketCap ═══

std::vector<DailyBarMarketCapRow> MarketDataRepository::queryDailyBarWithMarketCap(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    if (symbols.empty()) return {};
    std::ostringstream sql;
    sql << "SELECT si.symbol, d.trade_date, d.close, d.market_cap, d.circulating_market_cap"
        << " FROM mkt.daily_bar d JOIN ref.symbol_info si ON d.symbol_id = si.id"
        << " WHERE si.symbol IN " << symbolList(symbols)
        << " AND d.trade_date >= " << safeStr(startDate)
        << " AND d.trade_date <= " << safeStr(endDate)
        << " ORDER BY d.trade_date ASC, si.symbol ASC";
    auto qr = db_->executeQuery(sql.str());
    std::vector<DailyBarMarketCapRow> rows;
    rows.reserve(qr.rowCount());
    for (std::size_t i = 0; i < qr.rowCount(); ++i) {
        const auto& row = qr.getRow(i);
        DailyBarMarketCapRow out;
        out.symbol = row.getString("symbol");
        out.tradeDate = row.getString("trade_date");
        out.close = row.getDouble("close");
        out.marketCap = row.getDouble("market_cap");
        out.circulatingMarketCap = row.getDouble("circulating_market_cap");
        rows.push_back(std::move(out));
    }
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
        << cleaning::symbol_info_columns::sqlSelect() << ","
        << cleaning::money_flow_columns::sqlSelect()
        << " FROM mkt.daily_bar d JOIN ref.symbol_info s ON d.symbol_id = s.id"
        << " LEFT JOIN ref.industry_classification ic ON ic.symbol_id = d.symbol_id AND ic.end_date IS NULL"
        << " LEFT JOIN fund.money_flow_daily mf ON mf.symbol_id = d.symbol_id AND mf.trade_date = d.trade_date"
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
    sql << "SELECT TO_CHAR(MAX(trade_date), 'YYYYMMDD') AS td FROM data.trade_calendar"
        << " WHERE trade_date < " << safeStr(anchorDate);
    auto result = db_->executeQuery(sql.str());
    if (result.isEmpty()) return {};
    return result.getRow(0).getString("td");
}

// ═══ isTradingDay ═══

bool MarketDataRepository::isTradingDay(const std::string& date) {
    std::ostringstream sql;
    sql << "SELECT COUNT(*) AS cnt FROM data.trade_calendar"
        << " WHERE trade_date = " << safeStr(date) << " AND is_trading_day=1";
    auto result = db_->executeQuery(sql.str());
    if (result.isEmpty()) return false;
    return result.getRow(0).getInt("cnt") > 0;
}

// ═══ queryTradeCalendar ═══

std::vector<std::string> MarketDataRepository::queryTradeCalendar(
    const std::string& startDate, const std::string& endDate) {
    std::ostringstream sql;
    sql << "SELECT trade_date FROM data.trade_calendar"
        << " WHERE trade_date >= " << safeStr(startDate)
        << " AND trade_date <= " << safeStr(endDate)
        << " AND is_trading_day=1"
        << " ORDER BY trade_date";
    auto result = db_->executeQuery(sql.str());
    std::vector<std::string> dates;
    dates.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        dates.push_back(result.getRow(i).getString("trade_date"));
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

// ═══ queryMinuteDailyAgg ═══

std::vector<astock::database::SqlQueryResultRow>
MarketDataRepository::queryMinuteDailyAgg(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    if (symbols.empty()) return {};
    std::ostringstream sql;
    sql << "SELECT si.symbol, mb.trade_ts::date AS trade_date, "
        << cleaning::minute_daily_columns::sqlSelect()
        << " FROM mkt.minute_bar mb"
        << " JOIN ref.symbol_info si ON mb.symbol_id = si.id"
        << " WHERE si.symbol IN " << symbolList(symbols)
        << " AND mb.trade_ts >= " << safeStr(startDate + " 00:00:00")
        << " AND mb.trade_ts <= " << safeStr(endDate + " 23:59:59")
        << " GROUP BY si.symbol, mb.trade_ts::date"
        << " ORDER BY si.symbol, mb.trade_ts::date ASC";
    auto result = db_->executeQuery(sql.str());
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i)
        rows.push_back(result.getRow(i));
    return rows;
}

// ═══ queryMoneyFlow ═══

static std::string buildSymbolArray(const std::vector<std::string>& symbols) {
    std::ostringstream ss;
    ss << "ARRAY[";
    for (size_t i = 0; i < symbols.size(); ++i) {
        if (i) ss << ',';
        // 单引号转义（A股代码不含引号，防御性措施）
        std::string escaped = symbols[i];
        size_t pos = 0;
        while ((pos = escaped.find('\'', pos)) != std::string::npos) {
            escaped.insert(pos, "'");
            pos += 2;
        }
        ss << '\'' << escaped << '\'';
    }
    ss << ']';
    return ss.str();
}

std::vector<astock::database::SqlQueryResultRow>
MarketDataRepository::queryMoneyFlow(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    if (symbols.empty()) return {};

    std::ostringstream sql;
    sql << "SELECT si.symbol, mf.trade_date::text AS trade_date, "
        << cleaning::money_flow_columns::sqlSelect()
        << " FROM fund.money_flow_daily mf"
        << " JOIN ref.symbol_info si ON mf.symbol_id = si.id"
        << " WHERE si.symbol = ANY(" << buildSymbolArray(symbols) << ")"
        << " AND mf.trade_date BETWEEN " << safeStr(startDate)
        << " AND " << safeStr(endDate)
        << " ORDER BY si.symbol, mf.trade_date ASC";

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
    sql << "SELECT MIN(trade_date) AS td FROM data.trade_calendar"
        << " WHERE trade_date > " << safeStr(anchorDate);
    auto result = db_->executeQuery(sql.str());
    if (result.isEmpty()) return {};
    return result.getRow(0).getString("td");
}

// ═══ querySectorDailyAgg — 板块日频聚合 ═══

std::vector<astock::database::SqlQueryResultRow> MarketDataRepository::querySectorDailyAgg(
    const std::string& startDate,
    const std::string& endDate)
{
    // 使用 DataSourceRegistry 的 SQL 模板，替换日期占位符
    std::string sql = cleaning::sector_daily_columns::sqlSectorDailyAgg();
    // 替换占位符 {start_date}/{end_date}
    auto replace = [](std::string& s, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.length(), to);
            pos += to.length();
        }
    };
    replace(sql, "{start_date}", startDate);
    replace(sql, "{end_date}", endDate);

    auto result = db_->executeQuery(sql);
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i)
        rows.push_back(result.getRow(i));
    return rows;
}

// ═══ querySectorMoneyFlowAgg — 板块资金流日聚合 ═══

std::vector<astock::database::SqlQueryResultRow> MarketDataRepository::querySectorMoneyFlowAgg(
    const std::string& startDate,
    const std::string& endDate)
{
    std::string sql = cleaning::sector_daily_columns::sqlSectorMoneyFlowAgg();
    auto replace = [](std::string& s, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.length(), to);
            pos += to.length();
        }
    };
    replace(sql, "{start_date}", startDate);
    replace(sql, "{end_date}", endDate);

    auto result = db_->executeQuery(sql);
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i)
        rows.push_back(result.getRow(i));
    return rows;
}

// ═══ querySectorConcentration — 板块集中度 ═══

std::vector<astock::database::SqlQueryResultRow> MarketDataRepository::querySectorConcentration(
    const std::string& startDate,
    const std::string& endDate)
{
    std::string sql = cleaning::sector_daily_columns::sqlSectorConcentration();
    auto replace = [](std::string& s, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.length(), to);
            pos += to.length();
        }
    };
    replace(sql, "{start_date}", startDate);
    replace(sql, "{end_date}", endDate);

    auto result = db_->executeQuery(sql);
    std::vector<astock::database::SqlQueryResultRow> rows;
    rows.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i)
        rows.push_back(result.getRow(i));
    return rows;
}

// ═══════════════════════════════════════════════════════════════════
// live 交易链路: 日终持仓快照持久化
// ═══════════════════════════════════════════════════════════════════

bool MarketDataRepository::upsertDailyEquitySnapshot(const DailyEquitySnapshotInput& input,
                                                     std::string& outSnapshotId)
{
    using astock::database::SqlParam;
    const std::string sql =
        "INSERT INTO live.daily_equity_snapshots (id, strategy_id, trade_date, total_asset, daily_return) "
        "VALUES (gen_random_uuid()::varchar, ?, ?::date, ?, ?) "
        "ON CONFLICT (strategy_id, trade_date) DO UPDATE SET "
        "total_asset = EXCLUDED.total_asset, daily_return = EXCLUDED.daily_return "
        "RETURNING id";
    std::vector<SqlParam> params = {
        SqlParam{input.strategyId}, SqlParam{input.tradeDate},
        SqlParam{input.totalAsset}, SqlParam{input.dailyReturn}};
    auto result = db_->executeQuery(sql, params);
    if (result.isEmpty()) {
        INTERNAL_WARN_STREAM << "[日终入库] 权益快照 UPSERT 无返回: " << db_->lastError();
        return false;
    }
    outSnapshotId = result.getRow(0).getString("id");
    return !outSnapshotId.empty();
}

double MarketDataRepository::queryPrevDayTotalAsset(const std::string& strategyId,
                                                    const std::string& tradeDate)
{
    using astock::database::SqlParam;
    const std::string sql =
        "SELECT total_asset FROM live.daily_equity_snapshots "
        "WHERE strategy_id = ? AND trade_date < ?::date "
        "ORDER BY trade_date DESC LIMIT 1";
    auto result = db_->executeQuery(sql, {SqlParam{strategyId}, SqlParam{tradeDate}});
    return result.isEmpty() ? 0.0 : result.getRow(0).getDouble("total_asset");
}

int MarketDataRepository::upsertDailyPositions(const std::string& snapshotId,
                                               const std::string& tradeDate,
                                               const std::vector<DailyPositionRow>& rows)
{
    if (rows.empty()) return 0;
    using astock::database::SqlParam;
    // 标的匹配: 完整代码精确匹配, 纯代码按前缀匹配唯一行 (输入形式由调用方决定, 两分支天然互斥)
    const std::string sql =
        "INSERT INTO live.daily_position "
        "(summary_id, trade_date, symbol_id, position, avg_cost, market_value, floating_pnl, realized_pnl) "
        "SELECT ?::varchar, ?::date, si.id, ?::int, ?::numeric, ?::numeric, ?::numeric, ?::numeric "
        "FROM ref.symbol_info si "
        "WHERE si.symbol = ? OR si.symbol LIKE ? || '.%' "
        "ON CONFLICT (summary_id, trade_date, symbol_id) DO UPDATE SET "
        "position = EXCLUDED.position, avg_cost = EXCLUDED.avg_cost, "
        "market_value = EXCLUDED.market_value, floating_pnl = EXCLUDED.floating_pnl, "
        "realized_pnl = EXCLUDED.realized_pnl, created_at = now()";

    // 按纯代码去重 (同一标的只保留一行, 避免同批内键冲突)
    std::map<std::string, DailyPositionRow> unique;
    for (const auto& r : rows)
        unique[foundation::market::AStockSymbol::codeOnly(r.symbol)] = r;

    std::vector<std::vector<SqlParam>> batch;
    batch.reserve(unique.size());
    for (const auto& [code, r] : unique) {
        batch.push_back({
            SqlParam{snapshotId}, SqlParam{tradeDate},
            SqlParam{static_cast<std::int32_t>(r.quantity)},
            SqlParam{r.costPrice}, SqlParam{r.marketValue},
            SqlParam{r.floatingPnl}, SqlParam{r.realizedPnl},
            SqlParam{r.symbol}, SqlParam{code}});
    }
    return db_->executeBatchUpdate(sql, batch);
}

int MarketDataRepository::syncCurrentPositions(const std::vector<CurrentPositionRow>& rows)
{
    if (rows.empty()) return 0;
    using astock::database::SqlParam;
    // 策略归属: 空串 → NULL (手动持仓); first_held_at/hold_days 由 epoch 秒推导 (0 → 无持仓时间)
    const std::string upsertSql =
        "INSERT INTO live.current_position "
        "(symbol_id, strategy_id, quantity, available_qty, frozen_qty, avg_cost, "
        "last_price, market_value, unrealized_pnl, pnl_pct, prev_close, day_pnl, day_pnl_pct, "
        "first_held_at, held_days, updated_at) "
        "SELECT si.id, "
        "CASE WHEN ? = '' THEN NULL ELSE ?::varchar END, "
        "?::bigint, ?::bigint, ?::bigint, "
        "?::numeric, ?::numeric, ?::numeric, ?::numeric, ?::numeric, "
        "?::numeric, ?::numeric, ?::numeric, "
        "CASE WHEN ?::double precision <= 0 THEN NULL ELSE to_timestamp(?::double precision) END, "
        "CASE WHEN ?::double precision <= 0 THEN 0 "
        "     ELSE GREATEST(0, now()::date - to_timestamp(?::double precision)::date)::int END, "
        "now() "
        "FROM ref.symbol_info si "
        "WHERE si.symbol = ? OR si.symbol LIKE ? || '.%' "
        "ON CONFLICT (symbol_id) DO UPDATE SET "
        "strategy_id = EXCLUDED.strategy_id, quantity = EXCLUDED.quantity, "
        "available_qty = EXCLUDED.available_qty, frozen_qty = EXCLUDED.frozen_qty, "
        "avg_cost = EXCLUDED.avg_cost, last_price = EXCLUDED.last_price, "
        "market_value = EXCLUDED.market_value, unrealized_pnl = EXCLUDED.unrealized_pnl, "
        "pnl_pct = EXCLUDED.pnl_pct, prev_close = EXCLUDED.prev_close, "
        "day_pnl = EXCLUDED.day_pnl, day_pnl_pct = EXCLUDED.day_pnl_pct, "
        "first_held_at = EXCLUDED.first_held_at, held_days = EXCLUDED.held_days, "
        "updated_at = now()";
    const std::string deleteSql =
        "DELETE FROM live.current_position "
        "WHERE symbol_id = (SELECT id FROM ref.symbol_info "
        "                   WHERE symbol = ? OR symbol LIKE ? || '.%' LIMIT 1)";

    int total = 0;
    for (const auto& r : rows) {
        const std::string code = foundation::market::AStockSymbol::codeOnly(r.symbol);
        if (r.removed) {
            total += db_->executeUpdate(deleteSql, {SqlParam{r.symbol}, SqlParam{code}});
            continue;
        }
        std::vector<SqlParam> params = {
            SqlParam{r.strategyId}, SqlParam{r.strategyId},
            SqlParam{r.quantity}, SqlParam{r.availableQty}, SqlParam{r.frozenQty},
            SqlParam{r.costPrice}, SqlParam{r.lastPrice}, SqlParam{r.marketValue},
            SqlParam{r.unrealizedPnl}, SqlParam{r.pnlPct},
            SqlParam{r.prevClose}, SqlParam{r.dayPnl}, SqlParam{r.dayPnlPct},
            SqlParam{r.firstHeldAtEpochSec}, SqlParam{r.firstHeldAtEpochSec},
            SqlParam{r.firstHeldAtEpochSec}, SqlParam{r.firstHeldAtEpochSec},
            SqlParam{r.symbol}, SqlParam{code}};
        total += db_->executeUpdate(upsertSql, params);
    }
    return total;
}

} // namespace astock::infrastructure::database