// factor_enums.h
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <QString>
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

template <typename EnumType>
struct EnumNameEntry
{
    const char* name;
    EnumType value;
};

template <typename EnumType, size_t N>
inline EnumType enumFromNameEntries(const QString& rawType,
                                    const EnumNameEntry<EnumType> (&entries)[N],
                                    EnumType unknownValue)
{
    const QString normalized = rawType.trimmed().toLower();
    if (normalized.isEmpty()) {
        return unknownValue;
    }

    for (const EnumNameEntry<EnumType>& entry : entries) {
        if (normalized == QLatin1String(entry.name)) {
            return entry.value;
        }
    }

    return unknownValue;
}

inline TechnicalIndicator technicalIndicatorFromString(const QString& rawType)
{
    static const EnumNameEntry<TechnicalIndicator> entries[] = {
        {"rsi", TechnicalIndicator::RSI},
        {"macd", TechnicalIndicator::MACD},
        {"ma", TechnicalIndicator::MA},
        {"ema", TechnicalIndicator::EMA},
        {"boll", TechnicalIndicator::BOLL},
        {"kdj", TechnicalIndicator::KDJ},
        {"atr", TechnicalIndicator::ATR},
        {"obv", TechnicalIndicator::OBV},
        {"vwap", TechnicalIndicator::VWAP},
        {"volume_ratio", TechnicalIndicator::VOLUME_RATIO},
        {"turnover_stability", TechnicalIndicator::TURNOVER_STABILITY}
    };

    return enumFromNameEntries(rawType, entries, TechnicalIndicator::UNKNOWN);
}

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

inline MacroDimension macroDimensionFromString(const QString& rawType)
{
    static const EnumNameEntry<MacroDimension> entries[] = {
        {"growth", MacroDimension::GROWTH},
        {"inflation", MacroDimension::INFLATION},
        {"credit", MacroDimension::CREDIT},
        {"rates", MacroDimension::RATES},
        {"policy", MacroDimension::POLICY},
        {"risk_appetite", MacroDimension::RISK_APPETITE}
    };

    return enumFromNameEntries(rawType, entries, MacroDimension::UNKNOWN);
}

inline MacroIndicator macroIndicatorFromString(const QString& rawType)
{
    static const EnumNameEntry<MacroIndicator> entries[] = {
        {"industrial_added_value_yoy", MacroIndicator::INDUSTRIAL_ADDED_VALUE_YOY},
        {"manufacturing_pmi", MacroIndicator::MANUFACTURING_PMI},
        {"gdp_yoy", MacroIndicator::GDP_YOY},
        {"cpi_yoy", MacroIndicator::CPI_YOY},
        {"ppi_yoy", MacroIndicator::PPI_YOY},
        {"m2_yoy", MacroIndicator::M2_YOY},
        {"social_financing_stock_yoy", MacroIndicator::SOCIAL_FINANCING_STOCK_YOY},
        {"m1_m2_spread", MacroIndicator::M1_M2_SPREAD},
        {"ten_year_bond_yield", MacroIndicator::TEN_YEAR_BOND_YIELD},
        {"shibor_3m", MacroIndicator::SHIBOR_3M},
        {"lpr_1y", MacroIndicator::LPR_1Y},
        {"reserve_requirement_ratio", MacroIndicator::RESERVE_REQUIREMENT_RATIO},
        {"aa_credit_spread", MacroIndicator::AA_CREDIT_SPREAD},
        {"vix_proxy", MacroIndicator::VIX_PROXY}
    };

    return enumFromNameEntries(rawType, entries, MacroIndicator::UNKNOWN);
}

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