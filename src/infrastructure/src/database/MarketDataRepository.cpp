#include "database/MarketDataRepository.h"

#include <sstream>
#include <stdexcept>

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

// ═══ queryNextTradingDay ═══

std::string MarketDataRepository::queryNextTradingDay(const std::string& anchorDate) {
    std::ostringstream sql;
    sql << "SELECT MIN(trade_date) AS next_date FROM daily_bar"
        << " WHERE trade_date > " << safeStr(anchorDate);

    auto result = db_->executeQuery(sql.str());
    if (result.isEmpty()) return {};
    return result.getRow(0).getString("next_date");
}

} // namespace astock::infrastructure::database