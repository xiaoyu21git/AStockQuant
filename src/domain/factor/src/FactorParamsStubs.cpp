#include "DividendFactor.h"
#include "GrowthFactor.h"
#include "IndustryFactor.h"
#include "LiquidityFactor.h"
#include "MacroFactor.h"
#include "SentimentFactor.h"
#include "TechnicalFactor.h"
#include "factor_enums.h"
#include "foundation/json/json_facade.h"

#include <stdexcept>

namespace factor {

// ============================================================================
// DividendFactor::Params::fromJson
// ============================================================================
void DividendFactor::Params::fromJson(const foundation::json::JsonFacade& json)
{
    CommonParams::fromJson(json);

    if (json.has("dividendMetric")) {
        const auto& value = json.get("dividendMetric");
        if (!value.isNumber()) throw std::runtime_error("dividendMetric 不是枚举数值字段");
        const int v = value.asInt();
        if (v < static_cast<int>(DividendMetric::DIVIDEND_YIELD) || v > static_cast<int>(DividendMetric::UNKNOWN))
            throw std::runtime_error("dividendMetric 不是有效的枚举值");
        dividendMetric = static_cast<DividendMetric>(v);
    }
    if (json.has("dividendMetrics")) {
        dividendMetrics.clear();
        const auto& arr = json.get("dividendMetrics");
        if (!arr.isArray()) throw std::runtime_error("dividendMetrics 必须是数组");
        for (size_t i = 0; i < arr.size(); ++i) {
            const auto elem = arr.at(i);
            if (!elem.isNumber()) throw std::runtime_error("dividendMetrics 元素不是枚举数值");
            const int v = elem.asInt();
            dividendMetrics.push_back(static_cast<DividendMetric>(v));
        }
    }
    if (json.has("minDividendYield")) {
        minDividendYield = json.get("minDividendYield").asDouble();
    }
}

// ============================================================================
// GrowthFactor::Params::fromJson
// ============================================================================
void GrowthFactor::Params::fromJson(const foundation::json::JsonFacade& json)
{
    CommonParams::fromJson(json);

    if (json.has("growthMetrics")) {
        growthMetrics.clear();
        const auto& arr = json.get("growthMetrics");
        if (!arr.isArray()) throw std::runtime_error("growthMetrics 必须是数组");
        for (size_t i = 0; i < arr.size(); ++i) {
            const auto elem = arr.at(i);
            if (!elem.isNumber()) throw std::runtime_error("growthMetrics 元素不是枚举数值");
            const int v = elem.asInt();
            growthMetrics.push_back(static_cast<GrowthMetric>(v));
        }
    }
    if (json.has("growthWeights")) {
        growthWeights.clear();
        const auto& arr = json.get("growthWeights");
        if (!arr.isArray()) throw std::runtime_error("growthWeights 必须是数组");
        for (size_t i = 0; i < arr.size(); ++i) {
            const auto elem = arr.at(i);
            growthWeights.push_back(elem.asDouble());
        }
    }
}

// ============================================================================
// LiquidityFactor::Params::fromJson
// ============================================================================
void LiquidityFactor::Params::fromJson(const foundation::json::JsonFacade& json)
{
    CommonParams::fromJson(json);

    if (json.has("liquidityMetric")) {
        const auto& value = json.get("liquidityMetric");
        if (!value.isNumber()) throw std::runtime_error("liquidityMetric 不是枚举数值字段");
        const int v = value.asInt();
        if (v < static_cast<int>(LiquidityMetric::TURNOVER_RATE) || v > static_cast<int>(LiquidityMetric::UNKNOWN))
            throw std::runtime_error("liquidityMetric 不是有效的枚举值");
        liquidityMetric = static_cast<LiquidityMetric>(v);
    }
}

// ============================================================================
// IndustryFactor::Params::fromJson
// ============================================================================
void IndustryFactor::Params::fromJson(const foundation::json::JsonFacade& json)
{
    CommonParams::fromJson(json);

    if (json.has("industryMetricKind")) {
        const auto& value = json.get("industryMetricKind");
        if (!value.isNumber()) throw std::runtime_error("industryMetricKind 不是枚举数值字段");
        const int v = value.asInt();
        if (v < static_cast<int>(IndustryMetric::INDUSTRY_PROSPERITY) || v > static_cast<int>(IndustryMetric::UNKNOWN))
            throw std::runtime_error("industryMetricKind 不是有效的枚举值");
        industryMetricKind = static_cast<IndustryMetric>(v);
    }
    if (json.has("sectorType")) {
        const auto& value = json.get("sectorType");
        if (!value.isNumber()) throw std::runtime_error("sectorType 不是枚举数值字段");
        const int v = value.asInt();
        if (v < static_cast<int>(ConfigurableSectorType::SW_L1) || v > static_cast<int>(ConfigurableSectorType::Unknown))
            throw std::runtime_error("sectorType 不是有效的枚举值");
        sectorType = static_cast<ConfigurableSectorType>(v);
    }
}

// ============================================================================
// MacroFactor::Params::fromJson
// ============================================================================
void MacroFactor::Params::fromJson(const foundation::json::JsonFacade& json)
{
    CommonParams::fromJson(json);

    if (json.has("benchmarkSymbol")) {
        benchmarkSymbol = json.get("benchmarkSymbol").asString();
    }
    if (json.has("macroDimensions")) {
        macroDimensions.clear();
        const auto& arr = json.get("macroDimensions");
        if (!arr.isArray()) throw std::runtime_error("macroDimensions 必须是数组");
        for (size_t i = 0; i < arr.size(); ++i) {
            const auto elem = arr.at(i);
            if (!elem.isNumber()) throw std::runtime_error("macroDimensions 元素不是枚举数值");
            const int v = elem.asInt();
            macroDimensions.push_back(static_cast<MacroDimension>(v));
        }
    }
    if (json.has("macroIndicators")) {
        macroIndicators.clear();
        const auto& arr = json.get("macroIndicators");
        if (!arr.isArray()) throw std::runtime_error("macroIndicators 必须是数组");
        for (size_t i = 0; i < arr.size(); ++i) {
            const auto elem = arr.at(i);
            if (!elem.isNumber()) throw std::runtime_error("macroIndicators 元素不是枚举数值");
            const int v = elem.asInt();
            macroIndicators.push_back(static_cast<MacroIndicator>(v));
        }
    }
    if (json.has("macroFrequency")) {
        const auto& value = json.get("macroFrequency");
        if (!value.isNumber()) throw std::runtime_error("macroFrequency 不是枚举数值字段");
        const int v = value.asInt();
        if (v < static_cast<int>(DataFrequency::Daily) || v > static_cast<int>(DataFrequency::Yearly))
            throw std::runtime_error("macroFrequency 不是有效的枚举值");
        macroFrequency = static_cast<DataFrequency>(v);
    }
    if (json.has("macroWindow")) {
        macroWindow = json.get("macroWindow").asInt();
    }
    if (json.has("priceType")) {
        const auto& value = json.get("priceType");
        if (!value.isNumber()) throw std::runtime_error("priceType 不是枚举数值字段");
        const int v = value.asInt();
        if (v < static_cast<int>(TechnicalPriceType::CLOSE) || v > static_cast<int>(TechnicalPriceType::UNKNOWN))
            throw std::runtime_error("priceType 不是有效的枚举值");
        priceType = static_cast<TechnicalPriceType>(v);
    }
}

// ============================================================================
// SentimentFactor::Params::fromJson
// ============================================================================
void SentimentFactor::Params::fromJson(const foundation::json::JsonFacade& json)
{
    CommonParams::fromJson(json);

    if (json.has("sentimentMetric")) {
        const auto& value = json.get("sentimentMetric");
        if (!value.isNumber()) throw std::runtime_error("sentimentMetric 不是枚举数值字段");
        const int v = value.asInt();
        if (v < static_cast<int>(SentimentMetric::SENTIMENT_SCORE) || v > static_cast<int>(SentimentMetric::UNKNOWN))
            throw std::runtime_error("sentimentMetric 不是有效的枚举值");
        sentimentMetric = static_cast<SentimentMetric>(v);
    }
    if (json.has("sentimentSource")) {
        const auto& value = json.get("sentimentSource");
        if (!value.isNumber()) throw std::runtime_error("sentimentSource 不是枚举数值字段");
        const int v = value.asInt();
        if (v < static_cast<int>(SentimentSource::NEWS) || v > static_cast<int>(SentimentSource::UNKNOWN))
            throw std::runtime_error("sentimentSource 不是有效的枚举值");
        sentimentSource = static_cast<SentimentSource>(v);
    }
}

// ============================================================================
// TechnicalFactor::Params::fromJson
// ============================================================================
void TechnicalFactor::Params::fromJson(const foundation::json::JsonFacade& json)
{
    CommonParams::fromJson(json);

    if (json.has("technicalIndicators")) {
        technicalIndicators.clear();
        const auto& arr = json.get("technicalIndicators");
        if (!arr.isArray()) throw std::runtime_error("technicalIndicators 必须是数组");
        for (size_t i = 0; i < arr.size(); ++i) {
            const auto elem = arr.at(i);
            if (!elem.isNumber()) throw std::runtime_error("technicalIndicators 元素不是枚举数值");
            const int v = elem.asInt();
            technicalIndicators.push_back(static_cast<TechnicalIndicator>(v));
        }
    }
    if (json.has("technicalPriceType")) {
        const auto& value = json.get("technicalPriceType");
        if (!value.isNumber()) throw std::runtime_error("technicalPriceType 不是枚举数值字段");
        const int v = value.asInt();
        if (v < static_cast<int>(TechnicalPriceType::CLOSE) || v > static_cast<int>(TechnicalPriceType::UNKNOWN))
            throw std::runtime_error("technicalPriceType 不是有效的枚举值");
        technicalPriceType = static_cast<TechnicalPriceType>(v);
    }
    if (json.has("technicalCombinationMode")) {
        const auto& value = json.get("technicalCombinationMode");
        if (!value.isNumber()) throw std::runtime_error("technicalCombinationMode 不是枚举数值字段");
        const int v = value.asInt();
        if (v < static_cast<int>(TechnicalCombinationMode::EqualWeight) || v > static_cast<int>(TechnicalCombinationMode::NormalizedAverage))
            throw std::runtime_error("technicalCombinationMode 不是有效的枚举值");
        technicalCombinationMode = static_cast<TechnicalCombinationMode>(v);
    }

    if (json.has("rsiWindow"))             rsiWindow             = json.get("rsiWindow").asInt();
    if (json.has("maWindow"))              maWindow              = json.get("maWindow").asInt();
    if (json.has("emaWindow"))             emaWindow             = json.get("emaWindow").asInt();
    if (json.has("bollWindow"))            bollWindow            = json.get("bollWindow").asInt();
    if (json.has("bollStdDev"))            bollStdDev            = json.get("bollStdDev").asDouble();
    if (json.has("kdjWindow"))             kdjWindow             = json.get("kdjWindow").asInt();
    if (json.has("kdjKPeriod"))            kdjKPeriod            = json.get("kdjKPeriod").asInt();
    if (json.has("kdjDPeriod"))            kdjDPeriod            = json.get("kdjDPeriod").asInt();
    if (json.has("atrWindow"))             atrWindow             = json.get("atrWindow").asInt();
    if (json.has("macdFastPeriod"))        macdFastPeriod        = json.get("macdFastPeriod").asInt();
    if (json.has("macdSlowPeriod"))        macdSlowPeriod        = json.get("macdSlowPeriod").asInt();
    if (json.has("macdSignalPeriod"))      macdSignalPeriod      = json.get("macdSignalPeriod").asInt();
    if (json.has("obvWindow"))             obvWindow             = json.get("obvWindow").asInt();
    if (json.has("vwapWindow"))            vwapWindow            = json.get("vwapWindow").asInt();
    if (json.has("volumeRatioWindow"))     volumeRatioWindow     = json.get("volumeRatioWindow").asInt();
    if (json.has("turnoverStabilityWindow")) turnoverStabilityWindow = json.get("turnoverStabilityWindow").asInt();

    if (json.has("turnoverStabilityMetric")) {
        const auto& value = json.get("turnoverStabilityMetric");
        if (!value.isNumber()) throw std::runtime_error("turnoverStabilityMetric 不是枚举数值字段");
        const int v = value.asInt();
        if (v < static_cast<int>(LiquidityMetric::TURNOVER_RATE) || v > static_cast<int>(LiquidityMetric::UNKNOWN))
            throw std::runtime_error("turnoverStabilityMetric 不是有效的枚举值");
        turnoverStabilityMetric = static_cast<LiquidityMetric>(v);
    }
    if (json.has("useVolume")) {
        useVolume = json.get("useVolume").asBool();
    }
}

} // namespace factor