#pragma once

#include <cstdint>

namespace factor::compute {

/// @brief 标准字段键值常量（uint32_t 枚举，无 Qt 依赖）
/// 与 bridge 层 factor::bridge::DataFetchFieldContractUtils 定义的 FieldKey 语义一致
enum class StandardFieldKey : uint32_t {
    // ── OHLCV 基础价格字段（1-5）──
    Open   = 1,
    High   = 2,
    Low    = 3,
    Close  = 4,
    Volume = 5,

    // ── 日线行情扩展字段（6-15）──
    PbRatio               = 6,
    PeRatio               = 7,
    MarketCap             = 8,
    TurnoverRate          = 9,
    IndustryCode          = 10,
    CirculatingMarketCap  = 11,
    Turnover              = 12,
    ChangePct             = 13,
    Amplitude             = 14,
    PreClose              = 15,

    // ── 财务指标字段（50-60）──
    Roe              = 50,
    Roa              = 51,
    Eps              = 52,
    NetProfit        = 53,
    TotalRevenue     = 54,
    GrossMargin      = 55,
    OperatingMargin  = 56,
    DebtToEquity     = 57,
    CurrentRatio     = 58,
    OperatingCashFlow = 59,
    DividendYield    = 60,

    // ── 舆情/另类数据字段（70-80）──
    SentimentScore = 70,
    HotRank        = 80,
};

/// @brief 将 StandardFieldKey 转换为对应的字段名字符串
/// 注意：返回的字符串必须与 CachedMarketDataView::getField() 查询键完全一致
constexpr const char* fieldKeyToName(StandardFieldKey key) noexcept
{
    switch (key) {
    case StandardFieldKey::Open:                 return "open";
    case StandardFieldKey::High:                 return "high";
    case StandardFieldKey::Low:                  return "low";
    case StandardFieldKey::Close:                return "close";
    case StandardFieldKey::Volume:               return "volume";
    case StandardFieldKey::PbRatio:              return "pb_ratio";
    case StandardFieldKey::PeRatio:              return "pe_ratio";
    case StandardFieldKey::MarketCap:            return "market_cap";
    case StandardFieldKey::TurnoverRate:         return "turnover_rate";
    case StandardFieldKey::IndustryCode:         return "industry_code";
    case StandardFieldKey::CirculatingMarketCap: return "circulating_market_cap";
    case StandardFieldKey::Turnover:             return "turnover";
    case StandardFieldKey::ChangePct:            return "change_pct";
    case StandardFieldKey::Amplitude:            return "amplitude";
    case StandardFieldKey::PreClose:             return "pre_close";
    case StandardFieldKey::Roe:                  return "roe";
    case StandardFieldKey::Roa:                  return "roa";
    case StandardFieldKey::Eps:                  return "eps";
    case StandardFieldKey::NetProfit:            return "net_profit";
    case StandardFieldKey::TotalRevenue:         return "total_revenue";
    case StandardFieldKey::GrossMargin:          return "gross_margin";
    case StandardFieldKey::OperatingMargin:      return "operating_margin";
    case StandardFieldKey::DebtToEquity:         return "debt_to_equity";
    case StandardFieldKey::CurrentRatio:         return "current_ratio";
    case StandardFieldKey::OperatingCashFlow:    return "operating_cash_flow";
    case StandardFieldKey::DividendYield:        return "dividend_yield";
    case StandardFieldKey::SentimentScore:       return "sentiment_score";
    case StandardFieldKey::HotRank:              return "hot_rank";
    }
    return "close"; // 默认回退
}

} // namespace factor::compute