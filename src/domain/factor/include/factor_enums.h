// factor_enums.h
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <QMetaType>
namespace factor {
enum class FactorType : uint8_t {
    VALUE = 0,
    MOMENTUM = 1,
    SIZE = 2,
    QUALITY = 3,
    GROWTH = 4,
    DIVIDEND = 5,
    TECHNICAL = 6,
    LIQUIDITY = 7,
    MACRO = 8,
    INDUSTRY = 9,
    SENTIMENT = 10,
    CUSTOM = 11,
    LOW_VOLATILITY = 12,
    COMPOSITE = 13,
    UNKNOWN = 255
};

enum class CompositeCombineMode : uint8_t {
    WeightedAverage = 0,
    WeightedSum = 1,
    RankAverage = 2,
    Vote = 3,
    MaxScore = 4,
    MinScore = 5,
    UNKNOWN = 255
};

enum class CompositeNormalizeMode : uint8_t {
    None = 0,
    ZScore = 1,
    Rank = 2,
    Percentile = 3,
    WinsorizedZScore = 4,
    UNKNOWN = 255
};

enum class CompositeMissingPolicy : uint8_t {
    DropSymbol = 0,
    RenormalizeWeights = 1,
    FillNeutral = 2,
    RequireMinCoverage = 3,
    UNKNOWN = 255
};

enum class SourceTable : uint8_t {
    DAILY_BAR,
    FINANCIAL_INDICATOR,
    SYMBOL_INFO,
    NEWS_SENTIMENT,
    POLICY_DATA,
    ALTERNATIVE_DATA,
    DERIVATIVES_DATA,
    UNKNOWN
};

enum class ValuationMetric : uint8_t {
    BP,
    EP,
    DIVIDEND_YIELD,
    CFP,
    UNKNOWN
};

enum class SizeMetric : uint8_t {
    MARKET_CAP,
    CIRCULATING_MARKET_CAP,
    TOTAL_ASSETS,
    UNKNOWN
};

enum class GrowthMetric : uint8_t {
    REVENUE_GROWTH,
    NET_PROFIT_GROWTH,
    DELTA_ROE,
    SUE,
    UNKNOWN
};

enum class QualityMetric : uint8_t {
    ROE,
    ROA,
    GROSS_MARGIN,
    OPERATING_MARGIN,
    EARNINGS_QUALITY,
    UNKNOWN
};

enum class DividendMetric : uint8_t {
    DIVIDEND_YIELD,
    PAYOUT_RATIO,
    DIVIDEND_STABILITY,
    UNKNOWN
};

enum class LiquidityMetric : uint8_t {
    TURNOVER_RATE,
    VOLUME,
    AMIHUD_ILLIQUIDITY,
    AMPLITUDE,
    UNKNOWN
};

enum class IndustryMetric : uint8_t {
    INDUSTRY_PROSPERITY,
    INDUSTRY_MOMENTUM,
    INDUSTRY_CONCENTRATION,
    UNKNOWN
};

enum class SentimentMetric : uint8_t {
    SENTIMENT_SCORE,
    SOCIAL_SENTIMENT,
    INVESTOR_SENTIMENT,
    MARKET_SENTIMENT,
    UNKNOWN
};

enum class TechnicalIndicator : uint8_t {
    RSI,
    MACD,
    MA,
    EMA,
    BOLL,
    KDJ,
    ATR,
    OBV,
    VWAP,
    VOLUME_RATIO,
    TURNOVER_STABILITY,
    UNKNOWN
};

inline bool technicalIndicatorUsesPriceField(TechnicalIndicator indicator)
{
    switch (indicator) {
    case TechnicalIndicator::RSI:
    case TechnicalIndicator::MACD:
    case TechnicalIndicator::MA:
    case TechnicalIndicator::EMA:
    case TechnicalIndicator::BOLL:
    case TechnicalIndicator::KDJ:
    case TechnicalIndicator::ATR:
    case TechnicalIndicator::OBV:
    case TechnicalIndicator::VWAP:
        return true;
    default:
        return false;
    }
}

inline bool technicalIndicatorUsesHighLow(TechnicalIndicator indicator)
{
    switch (indicator) {
    case TechnicalIndicator::KDJ:
    case TechnicalIndicator::ATR:
        return true;
    default:
        return false;
    }
}

inline bool technicalIndicatorUsesVolume(TechnicalIndicator indicator)
{
    switch (indicator) {
    case TechnicalIndicator::OBV:
    case TechnicalIndicator::VWAP:
    case TechnicalIndicator::VOLUME_RATIO:
        return true;
    default:
        return false;
    }
}

inline bool technicalIndicatorUsesTurnoverMetric(TechnicalIndicator indicator)
{
    return indicator == TechnicalIndicator::TURNOVER_STABILITY;
}

enum class TechnicalPriceType : uint8_t {
    CLOSE,
    OPEN,
    HIGH,
    LOW,
    UNKNOWN
};

enum class SentimentSource : uint8_t {
    NEWS,
    SOCIAL_MEDIA,
    ANALYST_RATING,
    MARKET,
    POLICY,
    ALTERNATIVE,
    DERIVATIVES,
    UNKNOWN
};

enum class MacroDimension : uint8_t {
    GROWTH,
    INFLATION,
    CREDIT,
    RATES,
    POLICY,
    RISK_APPETITE,
    UNKNOWN
};

enum class MacroIndicator : uint8_t {
    INDUSTRIAL_ADDED_VALUE_YOY,
    MANUFACTURING_PMI,
    GDP_YOY,
    CPI_YOY,
    PPI_YOY,
    M2_YOY,
    SOCIAL_FINANCING_STOCK_YOY,
    M1_M2_SPREAD,
    TEN_YEAR_BOND_YIELD,
    SHIBOR_3M,
    LPR_1Y,
    RESERVE_REQUIREMENT_RATIO,
    AA_CREDIT_SPREAD,
    VIX_PROXY,
    UNKNOWN
};

inline MacroDimension macroIndicatorDimension(MacroIndicator indicator)
{
    switch (indicator) {
    case MacroIndicator::INDUSTRIAL_ADDED_VALUE_YOY:
    case MacroIndicator::MANUFACTURING_PMI:
    case MacroIndicator::GDP_YOY:
        return MacroDimension::GROWTH;
    case MacroIndicator::CPI_YOY:
    case MacroIndicator::PPI_YOY:
        return MacroDimension::INFLATION;
    case MacroIndicator::M2_YOY:
    case MacroIndicator::SOCIAL_FINANCING_STOCK_YOY:
    case MacroIndicator::M1_M2_SPREAD:
        return MacroDimension::CREDIT;
    case MacroIndicator::TEN_YEAR_BOND_YIELD:
    case MacroIndicator::SHIBOR_3M:
        return MacroDimension::RATES;
    case MacroIndicator::LPR_1Y:
    case MacroIndicator::RESERVE_REQUIREMENT_RATIO:
        return MacroDimension::POLICY;
    case MacroIndicator::AA_CREDIT_SPREAD:
    case MacroIndicator::VIX_PROXY:
        return MacroDimension::RISK_APPETITE;
    default:
        return MacroDimension::UNKNOWN;
    }
}

enum class NeutralizationMode : uint8_t {
    DISABLED,
    ENABLED
};

enum class AdjustPriceType : uint8_t {
    PRE_ADJUST_FACTOR,
    POST_ADJUST_FACTOR,
    UNKNOWN
};

enum class MarketEnvironmentProfile : uint8_t {
    GENERIC_EQUITY = 0,
    CN_A_SHARE = 1,
    HK_EQUITY = 2,
    US_EQUITY = 3
};

inline constexpr int marketEnvironmentProfileIndex(MarketEnvironmentProfile profile)
{
    return static_cast<int>(profile);
}

inline constexpr MarketEnvironmentProfile marketEnvironmentProfileFromIndex(int index)
{
    switch (index) {
    case 1:
        return MarketEnvironmentProfile::CN_A_SHARE;
    case 2:
        return MarketEnvironmentProfile::HK_EQUITY;
    case 3:
        return MarketEnvironmentProfile::US_EQUITY;
    case 0:
    default:
        return MarketEnvironmentProfile::GENERIC_EQUITY;
    }
}

enum class StrategyShortSellingMode : uint8_t {
    LONG_ONLY = 0,
    MARKET_ALLOWED_BUT_STRATEGY_DISABLED = 1,
    MARKET_AND_STRATEGY_ENABLED = 2
};

inline constexpr int strategyShortSellingModeIndex(StrategyShortSellingMode mode)
{
    return static_cast<int>(mode);
}

enum class StrategyExecutionPriceModel : uint8_t {
    MARKET_ON_CLOSE = 0,
    NEXT_SESSION_OPEN = 1
};

inline constexpr int strategyExecutionPriceModelIndex(StrategyExecutionPriceModel model)
{
    return static_cast<int>(model);
}

enum class StrategyReturnAttributionMode : uint8_t {
    POST_SIGNAL_SAME_TRADING_DAY_RETURN = 0,
    POST_SIGNAL_NEXT_TRADING_DAY_RETURN = 1
};

inline constexpr int strategyReturnAttributionModeIndex(StrategyReturnAttributionMode mode)
{
    return static_cast<int>(mode);
}

enum class StrategyPriceLimitMode : uint8_t {
    NONE = 0,
    DAILY_PRICE_LIMIT = 1
};

inline constexpr int strategyPriceLimitModeIndex(StrategyPriceLimitMode mode)
{
    return static_cast<int>(mode);
}

enum class StrategyCalendarProfile : uint8_t {
    GENERIC_EQUITY_TRADING_CALENDAR = 0,
    CN_A_SHARE_TRADING_CALENDAR = 1,
    HK_EQUITY_TRADING_CALENDAR = 2,
    US_EQUITY_TRADING_CALENDAR = 3
};

inline constexpr int strategyCalendarProfileIndex(StrategyCalendarProfile profile)
{
    return static_cast<int>(profile);
}

enum class StrategyCostProfile : uint8_t {
    GENERIC_EQUITY_DEFAULT_COST = 0,
    CN_A_SHARE_DEFAULT_COST = 1,
    HK_EQUITY_DEFAULT_COST = 2,
    US_EQUITY_DEFAULT_COST = 3
};

inline constexpr int strategyCostProfileIndex(StrategyCostProfile profile)
{
    return static_cast<int>(profile);
}

enum class MomentumCalculationType : uint8_t {
    SIMPLE,
    RANK,
    NORMALIZED,
    EXPONENTIAL,
    UNKNOWN
};

enum class LowVolComponent : uint8_t {
    VOLATILITY,
    DRAWDOWN,
    BETA,
    UNKNOWN
};

inline constexpr int factorTypeIndex(FactorType type)
{
    return type == FactorType::UNKNOWN ? -1 : static_cast<int>(type);
}

inline constexpr FactorType factorTypeFromIndex(int index)
{
    switch (index) {
    case 0: return FactorType::VALUE;
    case 1: return FactorType::MOMENTUM;
    case 2: return FactorType::SIZE;
    case 3: return FactorType::QUALITY;
    case 4: return FactorType::GROWTH;
    case 5: return FactorType::DIVIDEND;
    case 6: return FactorType::TECHNICAL;
    case 7: return FactorType::LIQUIDITY;
    case 8: return FactorType::MACRO;
    case 9: return FactorType::INDUSTRY;
    case 10: return FactorType::SENTIMENT;
    case 11: return FactorType::CUSTOM;
    case 12: return FactorType::LOW_VOLATILITY;
    case 13: return FactorType::COMPOSITE;
    default: return FactorType::UNKNOWN;
    }
}

}  // namespace factor

Q_DECLARE_METATYPE(factor::FactorType)
Q_DECLARE_METATYPE(factor::TechnicalIndicator)
Q_DECLARE_METATYPE(factor::MacroDimension)
Q_DECLARE_METATYPE(factor::MacroIndicator)
Q_DECLARE_METATYPE(factor::LiquidityMetric)
Q_DECLARE_METATYPE(factor::IndustryMetric)
Q_DECLARE_METATYPE(factor::SentimentMetric)
Q_DECLARE_METATYPE(factor::MarketEnvironmentProfile)