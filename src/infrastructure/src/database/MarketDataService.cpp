// MarketDataService.cpp — 纯 C++ 行情数据服务实现
#include "database/MarketDataService.h"
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace astock::infrastructure::database {

using J = foundation::json::JsonFacade;

// ── NaN/Inf 保护：将非有限浮点数转为合法 JSON 值 ──
namespace {
    /// @brief 检查 double 是否有限（非 NaN / Inf / -Inf）
    inline bool isFiniteDouble(double v) noexcept {
        return std::isfinite(v);
    }

    /// @brief 将 double 转为 JSON 数值字符串，NaN/Inf 替换为 "null"
    /// 用于 stringstream 直接拼接 JSON 场景，比 JsonFacade::toString() 更安全
    inline const char* safeJsonDouble(double v) noexcept {
        // 静态缓冲区，每个值覆盖上一次结果（用于 << 链式调用场景）
        static thread_local char buf[64];
        if (!std::isfinite(v)) {
            return "null";
        }
        // 使用足够精度，避免 snprintf 的 locale 相关行为
        int len = snprintf(buf, sizeof(buf), "%.10g", v);
        if (len < 0 || len >= static_cast<int>(sizeof(buf))) {
            return "null";
        }
        return buf;
    }

    /// @brief 对非有限值返回 null，否则返回 createDouble
    inline J safeCreateDouble(double v) {
        return std::isfinite(v) ? J::createDouble(v) : J::createNull();
    }

    /// @brief 字符串转义（用于 stringstream JSON 拼接）
    inline void appendJsonString(std::ostringstream& ss, const std::string& s) {
        ss << '"';
        for (char c : s) {
            if (c == '"') ss << "\\\"";
            else if (c == '\\') ss << "\\\\";
            else if (c == '\n') ss << "\\n";
            else if (c == '\r') ss << "\\r";
            else if (c == '\t') ss << "\\t";
            else ss << c;
        }
        ss << '"';
    }
} // anonymous namespace

// ── barRowToJson ──
J MarketDataService::barRowToJson(const DailyBarRow& row, const std::vector<std::string>& extraFields) {
    auto obj = J::createObject();
    obj.set(field::SYMBOL,     J::createString(row.symbol));
    obj.set(field::TRADE_DATE, J::createString(row.tradeDate));
    obj.set(field::OPEN,       safeCreateDouble(row.open));
    obj.set(field::HIGH,       safeCreateDouble(row.high));
    obj.set(field::LOW,        safeCreateDouble(row.low));
    obj.set(field::CLOSE,      safeCreateDouble(row.close));
    obj.set(field::VOLUME,     safeCreateDouble(row.volume));
    if (row.turnover > 0.0) {
        if (isFiniteDouble(row.turnover)) obj.set(field::TURNOVER, J::createDouble(row.turnover));
    }
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
    // 财务字段：使用 safeCreateDouble 保护 NaN/Inf
    obj.set(field::EPS,             safeCreateDouble(row.getDouble(field::EPS)));
    obj.set(field::BPS,             safeCreateDouble(row.getDouble(field::BPS)));
    obj.set(field::ROE,             safeCreateDouble(row.getDouble(field::ROE)));
    obj.set(field::ROA,             safeCreateDouble(row.getDouble(field::ROA)));
    obj.set(field::TOTAL_REVENUE,   safeCreateDouble(row.getDouble(field::TOTAL_REVENUE)));
    obj.set(field::NET_PROFIT,      safeCreateDouble(row.getDouble(field::NET_PROFIT)));
    obj.set(field::TOTAL_ASSETS,    safeCreateDouble(row.getDouble(field::TOTAL_ASSETS)));
    obj.set(field::TOTAL_LIABILITIES, safeCreateDouble(row.getDouble(field::TOTAL_LIABILITIES)));
    obj.set(field::EQUITY,          safeCreateDouble(row.getDouble(field::EQUITY)));
    obj.set(field::OPERATING_CF,    safeCreateDouble(row.getDouble(field::OPERATING_CF)));
    obj.set(field::INVESTING_CF,    safeCreateDouble(row.getDouble(field::INVESTING_CF)));
    obj.set(field::FINANCING_CF,    safeCreateDouble(row.getDouble(field::FINANCING_CF)));
    obj.set(field::DIVIDEND_YIELD,  safeCreateDouble(row.getDouble(field::DIVIDEND_YIELD)));
    return obj;
}

// ── genericRowToJson ──（通用：遍历所有列转 JSON）
J MarketDataService::genericRowToJson(const astock::database::SqlQueryResultRow& row) {
    auto obj = J::createObject();
    for (const auto& [col, val] : row.getValues()) {
        if (val.empty()) continue;
        char* end = nullptr;
        double d = strtod(val.c_str(), &end);
        if (end && end != val.c_str() && *end == '\0') {
            obj.set(col, safeCreateDouble(d));
        } else {
            obj.set(col, J::createString(val));
        }
    }
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
        switch (q.sourceType) {
        case DataSourceType::Financial:       return queryFinancialData(q.symbols, q.startDate, q.endDate);
        case DataSourceType::SymbolInfo:      return querySymbolInfo(q.symbols);
        case DataSourceType::NewsSentiment:   return queryNewsSentiment(q.symbols, q.startDate, q.endDate);
        case DataSourceType::PolicyData:      return queryPolicyData(q.symbols, q.startDate, q.endDate);
        case DataSourceType::AlternativeData: return queryAlternativeData(q.symbols, q.startDate, q.endDate);
        case DataSourceType::MinuteBar:       return queryMinuteBar(q.symbols, q.startDate, q.endDate);
        case DataSourceType::CleanedDailyBar: return queryCleanedDailyBar(q.symbols, q.startDate, q.endDate);
        case DataSourceType::AllMarket:       return queryAllMarket(q.startDate, q.endDate);
        case DataSourceType::Stock:
        case DataSourceType::Index:
        default: return queryDailyBar(q.symbols, q.startDate, q.endDate, q.extraFields);
        }
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

// ── queryWeeklyBar ──

MarketDataResult MarketDataService::queryWeeklyBar(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    MarketDataResult result;
    auto bars = m_repo->queryWeeklyBar(symbols, startDate, endDate);
    result.totalRows = static_cast<int>(bars.size());
    result.rows.reserve(bars.size());
    for (const auto& bar : bars) result.rows.push_back(barRowToJson(bar, {}));
    result.availableFields = {field::SYMBOL, field::TRADE_DATE, field::OPEN, field::HIGH,
                              field::LOW, field::CLOSE, field::VOLUME, field::TURNOVER};
    result.success = true;
    return result;
}

// ── queryMonthlyBar ──

MarketDataResult MarketDataService::queryMonthlyBar(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    MarketDataResult result;
    auto bars = m_repo->queryMonthlyBar(symbols, startDate, endDate);
    result.totalRows = static_cast<int>(bars.size());
    result.rows.reserve(bars.size());
    for (const auto& bar : bars) result.rows.push_back(barRowToJson(bar, {}));
    result.availableFields = {field::SYMBOL, field::TRADE_DATE, field::OPEN, field::HIGH,
                              field::LOW, field::CLOSE, field::VOLUME, field::TURNOVER};
    result.success = true;
    return result;
}

// ── queryMinuteBar ──

MarketDataResult MarketDataService::queryMinuteBar(
    const std::vector<std::string>& symbols,
    const std::string& startTime,
    const std::string& endTime)
{
    MarketDataResult result;
    auto rows = m_repo->queryMinuteBar(symbols, startTime, endTime);
    result.totalRows = static_cast<int>(rows.size());
    result.rows.reserve(rows.size());
    for (const auto& row : rows) result.rows.push_back(genericRowToJson(row));
    result.success = true;
    return result;
}

// ── queryNewsSentiment ──

MarketDataResult MarketDataService::queryNewsSentiment(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    MarketDataResult result;
    auto rows = m_repo->queryNewsSentiment(symbols, startDate, endDate);
    result.totalRows = static_cast<int>(rows.size());
    result.rows.reserve(rows.size());
    for (const auto& row : rows) result.rows.push_back(genericRowToJson(row));
    result.success = true;
    return result;
}

// ── queryPolicyData ──

MarketDataResult MarketDataService::queryPolicyData(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    MarketDataResult result;
    auto rows = m_repo->queryPolicyData(symbols, startDate, endDate);
    result.totalRows = static_cast<int>(rows.size());
    result.rows.reserve(rows.size());
    for (const auto& row : rows) result.rows.push_back(genericRowToJson(row));
    result.success = true;
    return result;
}

// ── queryAlternativeData ──

MarketDataResult MarketDataService::queryAlternativeData(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    MarketDataResult result;
    auto rows = m_repo->queryAlternativeData(symbols, startDate, endDate);
    result.totalRows = static_cast<int>(rows.size());
    result.rows.reserve(rows.size());
    for (const auto& row : rows) result.rows.push_back(genericRowToJson(row));
    result.success = true;
    return result;
}

// ── queryCleanedDailyBar ──

MarketDataResult MarketDataService::queryCleanedDailyBar(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate)
{
    MarketDataResult result;
    auto rows = m_repo->queryCleanedDailyBar(symbols, startDate, endDate);
    result.totalRows = static_cast<int>(rows.size());
    result.rows.reserve(rows.size());
    for (const auto& row : rows) result.rows.push_back(genericRowToJson(row));
    result.success = true;
    return result;
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
    // 使用 safeJsonDouble() 和 appendJsonString() 保护 NaN/Inf 和特殊字符
    std::ostringstream ss;
    ss << '[';
    bool firstRow = true;
    for (const auto& bar : bars) {
        if (!firstRow) ss << ',';
        firstRow = false;
        ss << "{\"symbol\":";
        appendJsonString(ss, bar.symbol);
        ss << ",\"trade_date\":";
        appendJsonString(ss, bar.tradeDate);
        ss << ",\"open\":" << safeJsonDouble(bar.open)
           << ",\"high\":" << safeJsonDouble(bar.high)
           << ",\"low\":" << safeJsonDouble(bar.low)
           << ",\"close\":" << safeJsonDouble(bar.close)
           << ",\"volume\":" << safeJsonDouble(bar.volume);
        if (bar.turnover > 0.0) ss << ",\"turnover\":" << safeJsonDouble(bar.turnover);
        ss << '}';
    }
    ss << ']';
    return ss.str();
}

} // namespace astock::infrastructure::database
