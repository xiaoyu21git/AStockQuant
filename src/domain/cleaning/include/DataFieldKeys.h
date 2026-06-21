// DataFieldKeys.h — 纯 C++ 数据字段名常量（零 Qt 依赖）
// 与 DataFetchFieldContractUtils.h 保持字段名一致
// bin 缓存、数据清洗、因子计算统一使用此文件定义的字段名
#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <initializer_list>

namespace cleaning {

class FieldKey {
public:
    constexpr explicit FieldKey(const char* name) : m_name(name) {}
    const char* c_str() const { return m_name; }
    operator const char*() const { return m_name; }
    bool operator==(const FieldKey& o) const { return m_name == o.m_name; }
    bool operator==(const std::string& s) const { return m_name == s; }
private:
    const char* m_name;
};

struct FieldKeyHash {
    std::size_t operator()(const FieldKey& k) const {
        return std::hash<const char*>{}(k.c_str());
    }
};

using FieldSet = std::unordered_set<FieldKey, FieldKeyHash>;

// ── 基础字段 ──
struct CF {  // CommonFields
    static constexpr FieldKey SYMBOL{"symbol"};
    static constexpr FieldKey TRADE_DATE{"trade_date"};
    static constexpr FieldKey DATA_SOURCE{"data_source"};
};

// ── 行情字段 (与 MarketBarFieldKeys 对齐) ──
struct MF {  // MarketFields
    static constexpr FieldKey OPEN{"open"};
    static constexpr FieldKey HIGH{"high"};
    static constexpr FieldKey LOW{"low"};
    static constexpr FieldKey CLOSE{"close"};
    static constexpr FieldKey PRE_CLOSE{"pre_close"};
    static constexpr FieldKey VOLUME{"volume"};
    static constexpr FieldKey TURNOVER{"turnover"};
    static constexpr FieldKey CHANGE_AMT{"change_amt"};
    static constexpr FieldKey CHANGE_PCT{"change_pct"};
    static constexpr FieldKey AMPLITUDE{"amplitude"};
    static constexpr FieldKey TURNOVER_RATE{"turnover_rate"};
    static constexpr FieldKey PRE_ADJ_FACTOR{"pre_adjust_factor"};
    static constexpr FieldKey POST_ADJ_FACTOR{"post_adjust_factor"};
    static constexpr FieldKey MARKET_CAP{"market_cap"};
    static constexpr FieldKey CIRCULATING_MARKET_CAP{"circulating_market_cap"};
    static constexpr FieldKey PE_RATIO{"pe_ratio"};
    static constexpr FieldKey PB_RATIO{"pb_ratio"};
    static constexpr FieldKey INDUSTRY_CODE{"industry_code"};

    static constexpr FieldKey ohlcvFields[] = {OPEN, HIGH, LOW, CLOSE, VOLUME};
    static constexpr size_t ohlcvCount = 5;

    static FieldSet priceCore() { return {OPEN, HIGH, LOW, CLOSE}; }
    static FieldSet missingFillDefaults() { return {OPEN, HIGH, LOW, CLOSE, TURNOVER_RATE, MARKET_CAP, CIRCULATING_MARKET_CAP}; }
    static FieldSet all() { return {OPEN, HIGH, LOW, CLOSE, PRE_CLOSE, VOLUME, TURNOVER, CHANGE_AMT, CHANGE_PCT, AMPLITUDE, TURNOVER_RATE, PRE_ADJ_FACTOR, POST_ADJ_FACTOR, MARKET_CAP, CIRCULATING_MARKET_CAP, PE_RATIO, PB_RATIO, INDUSTRY_CODE}; }
};

// ── 财务字段 ──
struct F_F {  // FinancialFields
    static constexpr FieldKey REPORT_DATE{"report_date"};
    static constexpr FieldKey REPORT_TYPE{"report_type"};
    static constexpr FieldKey DISCLOSURE_DATE{"disclosure_date"};
    static constexpr FieldKey EFFECTIVE_DISCLOSURE_DATE{"effective_disclosure_date"};
    static constexpr FieldKey SYMBOL_ID{"symbol_id"};
    static constexpr FieldKey INDICATOR_ID{"indicator_id"};
    static constexpr FieldKey EPS{"eps"};
    static constexpr FieldKey BPS{"bps"};
    static constexpr FieldKey ROA{"roa"};
    static constexpr FieldKey ROE{"roe"};
    static constexpr FieldKey PROFIT_MARGIN{"profit_margin"};
    static constexpr FieldKey GROSS_MARGIN{"gross_margin"};
    static constexpr FieldKey OPERATING_MARGIN{"operating_margin"};
    static constexpr FieldKey DEBT_TO_EQUITY{"debt_to_equity"};
    static constexpr FieldKey CURRENT_RATIO{"current_ratio"};
    static constexpr FieldKey QUICK_RATIO{"quick_ratio"};
    static constexpr FieldKey OPERATING_CASH_FLOW{"operating_cash_flow"};
    static constexpr FieldKey INVESTING_CASH_FLOW{"investing_cash_flow"};
    static constexpr FieldKey FINANCING_CASH_FLOW{"financing_cash_flow"};
    static constexpr FieldKey TOTAL_REVENUE{"total_revenue"};
    static constexpr FieldKey NET_PROFIT{"net_profit"};
    static constexpr FieldKey TOTAL_ASSETS{"total_assets"};
    static constexpr FieldKey TOTAL_LIABILITIES{"total_liabilities"};
    static constexpr FieldKey EQUITY{"equity"};
    static constexpr FieldKey DIVIDEND_YIELD{"dividend_yield"};
    static constexpr FieldKey PAYOUT_RATIO{"payout_ratio"};
    static constexpr FieldKey DIVIDEND_STABILITY{"dividend_stability"};
};

// ── 财务字段 (与 FinancialFieldKeys 对齐) ──
struct FF {
    static constexpr FieldKey REPORT_DATE{"report_date"};
    static constexpr FieldKey DISCLOSURE_DATE{"disclosure_date"};
    static constexpr FieldKey BPS{"bps"};
    static constexpr FieldKey TOTAL_ASSETS{"total_assets"};
    static constexpr FieldKey NET_PROFIT{"net_profit"};
    static constexpr FieldKey EQUITY{"equity"};
    static constexpr FieldKey ROE{"roe"};
    static constexpr FieldKey ROA{"roa"};
    static constexpr FieldKey EPS{"eps"};
    static constexpr FieldKey TOTAL_REVENUE{"total_revenue"};
    static constexpr FieldKey TOTAL_LIABILITIES{"total_liabilities"};
    static constexpr FieldKey DIVIDEND_YIELD{"dividend_yield"};
    static constexpr FieldKey OPERATING_CASH_FLOW{"operating_cash_flow"};
    static constexpr FieldKey INVESTING_CASH_FLOW{"investing_cash_flow"};
    static constexpr FieldKey FINANCING_CASH_FLOW{"financing_cash_flow"};
    static constexpr FieldKey PROFIT_MARGIN{"profit_margin"};
    static constexpr FieldKey GROSS_MARGIN{"gross_margin"};
    static constexpr FieldKey OPERATING_MARGIN{"operating_margin"};
    static constexpr FieldKey DEBT_TO_EQUITY{"debt_to_equity"};
    static constexpr FieldKey CURRENT_RATIO{"current_ratio"};
    static constexpr FieldKey QUICK_RATIO{"quick_ratio"};
    static constexpr FieldKey PAYOUT_RATIO{"payout_ratio"};
    static constexpr FieldKey NET_MARGIN{"net_margin"};
    static constexpr FieldKey DIVIDEND_STABILITY{"dividend_stability"};
};

// ── 上下文字段 ──
struct XF {
    static constexpr FieldKey LIST_DATE{"list_date"};
    static constexpr FieldKey DELIST_DATE{"delist_date"};
    static constexpr FieldKey STATUS{"status"};
    static constexpr FieldKey EXCHANGE{"exchange"};
    static constexpr FieldKey NAME{"name"};
};

// ── 清洗标签字段 ──
struct TF {  // TagFields
    static constexpr FieldKey VALUATION_SANITIZED{"valuation_sanitized"};
    static constexpr FieldKey SURVIVOR_BIAS_CHECKED{"survivor_bias_checked"};
    static constexpr FieldKey REPORT_DATE_ALIGNED{"report_date_aligned"};
    static constexpr FieldKey IS_SUSPENDED{"is_suspended"};
    static constexpr FieldKey SUSPENSION_DAYS{"suspension_days"};
    static constexpr FieldKey FORWARD_FILLED{"forward_filled"};
    static constexpr FieldKey MISSING_VALUE_FILLED{"missing_value_filled"};
    static constexpr FieldKey LIMIT_UP{"limit_up"};
    static constexpr FieldKey LIMIT_DOWN{"limit_down"};
    static constexpr FieldKey CAN_BUY{"can_buy"};
    static constexpr FieldKey CAN_SELL{"can_sell"};
};

// ── 清洗内部字段 ──
struct IF {  // InternalFields
    static constexpr FieldKey DATA_TYPE{"data_type"};
    static constexpr FieldKey ADJUSTED_PRICE_APPLIED{"adjusted_price_applied"};
    static constexpr FieldKey VALUATION_INVALID_FIELDS{"valuation_invalid_fields"};
    static constexpr FieldKey CLEANING_TAGS{"cleaning_tags"};
};

// ── bin 缓存用：非 OHLCV 的额外字段 ──
inline std::vector<std::string> extraFieldNames(const std::vector<std::string>& availableFields) {
    static const std::unordered_set<std::string> kOhlcv{
        "open","high","low","close","volume","symbol","trade_date"
    };
    std::vector<std::string> extra;
    for (const auto& f : availableFields) {
        if (!kOhlcv.count(f)) extra.push_back(f);
    }
    return extra;
}

} // namespace cleaning
