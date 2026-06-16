// MarketDataService.cpp — 纯 C++ 行情数据服务实现
#include "database/MarketDataService.h"
#include <cstdio>
#include <algorithm>
#include <sstream>

namespace astock::infrastructure::database {

using J = foundation::json::JsonFacade;

// ── barRowToJson ──
J MarketDataService::barRowToJson(const DailyBarRow& row, const std::vector<std::string>& extraFields) {
    auto obj = J::createObject();
    obj.set(field::SYMBOL,     J::createString(row.symbol));
    obj.set(field::TRADE_DATE, J::createString(row.tradeDate));
    obj.set(field::OPEN,       J::createDouble(row.open));
    obj.set(field::HIGH,       J::createDouble(row.high));
    obj.set(field::LOW,        J::createDouble(row.low));
    obj.set(field::CLOSE,      J::createDouble(row.close));
    obj.set(field::VOLUME,     J::createDouble(row.volume));
    if (row.turnover > 0.0) obj.set(field::TURNOVER, J::createDouble(row.turnover));
    for (const auto& f : extraFields) (void)f; // extra fields stored in SQL result rows
    return obj;
}

// ── financialRowToJson ──
J MarketDataService::financialRowToJson(const astock::database::SqlQueryResultRow& row) {
    auto obj = J::createObject();
    obj.set(field::SYMBOL,          J::createString(row.getString(field::SYMBOL)));
    obj.set(field::REPORT_DATE,     J::createString(row.getString(field::REPORT_DATE)));
    if (row.contains(field::DISCLOSURE_DATE))
        obj.set(field::DISCLOSURE_DATE, J::createString(row.getString(field::DISCLOSURE_DATE)));
    obj.set(field::EPS,             J::createDouble(row.getDouble(field::EPS)));
    obj.set(field::BPS,             J::createDouble(row.getDouble(field::BPS)));
    obj.set(field::ROE,             J::createDouble(row.getDouble(field::ROE)));
    obj.set(field::ROA,             J::createDouble(row.getDouble(field::ROA)));
    obj.set(field::TOTAL_REVENUE,   J::createDouble(row.getDouble(field::TOTAL_REVENUE)));
    obj.set(field::NET_PROFIT,      J::createDouble(row.getDouble(field::NET_PROFIT)));
    obj.set(field::TOTAL_ASSETS,    J::createDouble(row.getDouble(field::TOTAL_ASSETS)));
    obj.set(field::TOTAL_LIABILITIES, J::createDouble(row.getDouble(field::TOTAL_LIABILITIES)));
    obj.set(field::EQUITY,          J::createDouble(row.getDouble(field::EQUITY)));
    obj.set(field::OPERATING_CF,    J::createDouble(row.getDouble(field::OPERATING_CF)));
    obj.set(field::INVESTING_CF,    J::createDouble(row.getDouble(field::INVESTING_CF)));
    obj.set(field::FINANCING_CF,    J::createDouble(row.getDouble(field::FINANCING_CF)));
    obj.set(field::DIVIDEND_YIELD,  J::createDouble(row.getDouble(field::DIVIDEND_YIELD)));
    return obj;
}

// ── symbolInfoRowToJson ──
J MarketDataService::symbolInfoRowToJson(const astock::database::SqlQueryResultRow& row) {
    auto obj = J::createObject();
    obj.set(field::SYMBOL,    J::createString(row.getString(field::SYMBOL)));
    obj.set(field::NAME,      J::createString(row.getString(field::NAME)));
    obj.set(field::EXCHANGE,  J::createString(row.getString(field::EXCHANGE)));
    obj.set(field::ASSET_CLASS, J::createString(row.getString(field::ASSET_CLASS)));
    obj.set(field::LIST_DATE, J::createString(row.getString(field::LIST_DATE)));
    obj.set(field::DELIST_DATE, J::createString(row.getString(field::DELIST_DATE)));
    obj.set(field::STATUS,    J::createString(row.getString(field::STATUS)));
    return obj;
}

// ── query ──
MarketDataResult MarketDataService::query(const MarketDataQuery& q, ProgressCallback onProgress) {
    MarketDataResult result;
    try {
        if (q.sourceType == DataSourceType::Financial) {
            return queryFinancialData(q.symbols, q.startDate, q.endDate);
        }
        if (q.sourceType == DataSourceType::SymbolInfo) {
            return querySymbolInfo(q.symbols);
        }
        if (q.sourceType == DataSourceType::AllMarket) {
            return queryAllMarket(q.startDate, q.endDate);
        }
        // Stock / Index: daily_bar with extra fields
        return queryDailyBar(q.symbols, q.startDate, q.endDate, q.extraFields);
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    return result;
}

MarketDataResult MarketDataService::queryDailyBar(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate,
    const std::vector<std::string>& extraFields)
{
    MarketDataResult result;
    if (symbols.empty()) { result.success = true; return result; }

    auto bars = m_repo->queryDailyBarWithFields(symbols, startDate, endDate, extraFields);
    result.totalRows = static_cast<int>(bars.size());
    result.rows.reserve(bars.size());
    for (const auto& bar : bars) {
        result.rows.push_back(barRowToJson(bar, extraFields));
    }
    // 可用字段
    result.availableFields = {field::SYMBOL, field::TRADE_DATE, field::OPEN, field::HIGH, field::LOW,
                              field::CLOSE, field::VOLUME, field::TURNOVER};
    for (const auto& f : extraFields) result.availableFields.push_back(f);
    result.success = true;
    return result;
}

MarketDataResult MarketDataService::queryAllMarket(const std::string& startDate, const std::string& endDate) {
    MarketDataResult result;
    auto bars = m_repo->queryAllMarketDailyBar(startDate, endDate);
    result.totalRows = static_cast<int>(bars.size());
    result.rows.reserve(bars.size());
    for (const auto& bar : bars) result.rows.push_back(barRowToJson(bar, {}));
    result.availableFields = {field::SYMBOL, field::TRADE_DATE, field::OPEN, field::HIGH,
                              field::LOW, field::CLOSE, field::VOLUME, field::TURNOVER};
    result.success = true;
    return result;
}

std::vector<std::string> MarketDataService::queryIndexConstituents(const std::string& indexSymbol, const std::string& date) {
    return m_repo->queryIndexConstituents(indexSymbol, date);
}

std::vector<std::string> MarketDataService::queryIndexList() {
    return m_repo->queryIndexList();
}

MarketDataResult MarketDataService::queryFinancialData(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    MarketDataResult result;
    auto rows = m_repo->queryFinancialData(symbols, startDate, endDate);
    result.totalRows = static_cast<int>(rows.size());
    result.rows.reserve(rows.size());
    for (const auto& row : rows) result.rows.push_back(financialRowToJson(row));
    result.availableFields = {field::SYMBOL, field::REPORT_DATE, field::DISCLOSURE_DATE,
        field::EPS, field::BPS, field::ROE, field::ROA, field::TOTAL_REVENUE, field::NET_PROFIT,
        field::TOTAL_ASSETS, field::TOTAL_LIABILITIES, field::EQUITY,
        field::OPERATING_CF, field::INVESTING_CF, field::FINANCING_CF, field::DIVIDEND_YIELD};
    result.success = true;
    return result;
}

MarketDataResult MarketDataService::querySymbolInfo(const std::vector<std::string>& symbols) {
    MarketDataResult result;
    auto rows = m_repo->querySymbolInfo(symbols);
    result.totalRows = static_cast<int>(rows.size());
    result.rows.reserve(rows.size());
    for (const auto& row : rows) result.rows.push_back(symbolInfoRowToJson(row));
    result.availableFields = {field::SYMBOL, field::NAME, field::EXCHANGE, field::ASSET_CLASS,
                              field::LIST_DATE, field::DELIST_DATE, field::STATUS};
    result.success = true;
    return result;
}

std::string MarketDataService::nextTradingDay(const std::string& anchorDate) {
    return m_repo->queryNextTradingDay(anchorDate);
}

// ── 高效批量 JSON 输出（绕过逐行 JsonFacade 构建）──
std::string MarketDataService::queryDailyBarAsJson(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate,
    const std::vector<std::string>& extraFields)
{
    if (symbols.empty()) return "[]";
    auto bars = m_repo->queryDailyBarWithFields(symbols, startDate, endDate, extraFields);

    // 用 stringstream 直接构建 JSON，比 JsonFacade 逐行构建快 3-5x
    std::ostringstream ss;
    ss << '[';
    bool firstRow = true;
    for (const auto& bar : bars) {
        if (!firstRow) ss << ',';
        firstRow = false;
        ss << "{\"symbol\":\"" << bar.symbol << "\","
           << "\"trade_date\":\"" << bar.tradeDate << "\","
           << "\"open\":" << bar.open << ','
           << "\"high\":" << bar.high << ','
           << "\"low\":" << bar.low << ','
           << "\"close\":" << bar.close << ','
           << "\"volume\":" << bar.volume;
        if (bar.turnover > 0.0) ss << ",\"turnover\":" << bar.turnover;
        ss << '}';
    }
    ss << ']';
    return ss.str();
}

} // namespace astock::infrastructure::database
