#include "domain/factor/include/ConfigurableFactorDetail.h"
#include "domain/indicators/include/batch_technical_indicators.h"
#include "ui/bridge/include/DataFetchFieldContractUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace factor {

using namespace configurable_factor_detail;

namespace {

namespace technical_metadata {

constexpr const char* kEmptyReasonKey = "emptyReason";
constexpr const char* kTechnicalConfigModeKey = "technicalConfigMode";
constexpr const char* kIndicatorTypeKey = "indicatorType";
constexpr const char* kIndicatorTypesKey = "indicatorTypes";
constexpr const char* kErrorKey = "error";
constexpr const char* kEffectiveDateKey = "effectiveDate";
constexpr const char* kFrequencyKey = "frequency";
constexpr const char* kLookbackWindowKey = "lookbackWindow";
constexpr const char* kLaggedEnabledKey = "laggedEnabled";
constexpr const char* kStandardizationKey = "standardization";
constexpr const char* kNeutralizationEnabledKey = "neutralizationEnabled";
constexpr const char* kNeutralizationModeKey = "neutralizationMode";
constexpr const char* kSymbolCountKey = "symbolCount";
constexpr const char* kPriceTypeKey = "priceType";
constexpr const char* kPriceSourceTableKey = "priceSourceTable";
constexpr const char* kActualPriceTypeKey = "actualPriceType";
constexpr const char* kPriceFieldDerivedKey = "priceFieldDerived";
constexpr const char* kUseVolumeKey = "useVolume";
constexpr const char* kWindowKey = "window";
constexpr const char* kTechnicalCombinationModeKey = "technicalCombinationMode";
constexpr const char* kMaWindowKey = "maWindow";
constexpr const char* kEmaWindowKey = "emaWindow";
constexpr const char* kBollWindowKey = "bollWindow";
constexpr const char* kBollStdDevKey = "bollStdDev";
constexpr const char* kKdjWindowKey = "kdjWindow";
constexpr const char* kKdjKPeriodKey = "kdjKPeriod";
constexpr const char* kKdjDPeriodKey = "kdjDPeriod";
constexpr const char* kAtrWindowKey = "atrWindow";
constexpr const char* kMacdFastPeriodKey = "macdFastPeriod";
constexpr const char* kMacdSlowPeriodKey = "macdSlowPeriod";
constexpr const char* kMacdSignalPeriodKey = "macdSignalPeriod";
constexpr const char* kObvWindowKey = "obvWindow";
constexpr const char* kVwapWindowKey = "vwapWindow";
constexpr const char* kVolumeRatioWindowKey = "volumeRatioWindow";
constexpr const char* kTurnoverStabilityWindowKey = "turnoverStabilityWindow";
constexpr const char* kTurnoverStabilityMetricKey = "turnoverStabilityMetric";
constexpr const char* kTurnoverStabilitySourceTableKey = "turnoverStabilitySourceTable";

} // namespace technical_metadata

void setTechnicalIndicatorContextMetadata(CalculationResult& result,
                                         TechnicalConfigMode technicalConfigMode,
                                         const std::vector<TechnicalIndicator>& indicatorTypes)
{
    result.metadata.set(technical_metadata::kTechnicalConfigModeKey,
                        json_helper::toJsonValue(static_cast<int>(technicalConfigMode)));
    result.metadata.set(technical_metadata::kIndicatorTypesKey, technicalIndicatorArrayJson(indicatorTypes));
}

void setTechnicalCommonRuntimeMetadata(CalculationResult& result,
                                       const std::string& effectiveDate,
                                       DataFrequency frequency,
                                       const ConfigurableFactorBase::CommonParams& common,
                                       StandardizationMethod standardization,
                                       NeutralizationStatus neutralizationMode,
                                       int symbolCount)
{
    result.metadata.set(technical_metadata::kEffectiveDateKey, json_helper::toJsonValue(effectiveDate));
    result.metadata.set(technical_metadata::kFrequencyKey, json_helper::toJsonValue(static_cast<int>(frequency)));
    result.metadata.set(technical_metadata::kLookbackWindowKey, json_helper::toJsonValue(common.lookbackWindow));
    result.metadata.set(technical_metadata::kLaggedEnabledKey, json_helper::toJsonValue(common.lagEnabled));
    result.metadata.set(technical_metadata::kStandardizationKey, json_helper::toJsonValue(static_cast<int>(standardization)));
    result.metadata.set(technical_metadata::kNeutralizationEnabledKey, json_helper::toJsonValue(common.neutralizationEnabled));
    result.metadata.set(technical_metadata::kNeutralizationModeKey,
                        json_helper::toJsonValue(static_cast<int>(neutralizationMode)));
    result.metadata.set(technical_metadata::kSymbolCountKey, json_helper::toJsonValue(symbolCount));
}

} // namespace

CalculationResult ConfigurableFactorBase::calculateTechnical(const CalculationContext& context) const
{
    const CommonParams& common = commonParams_;
    const TechnicalParams& technical = technicalParams();
    const DataFrequency frequency = common.frequency;

    std::vector<TechnicalIndicator> indicatorTypes;
    const TechnicalConfigMode technicalConfigMode = !technical.technicalIndicators.empty()
        ? TechnicalConfigMode::TechnicalIndicators
        : TechnicalConfigMode::MissingTechnicalIndicators;

    for (const TechnicalIndicator indicatorType : technical.technicalIndicators) {
        if (indicatorType != TechnicalIndicator::UNKNOWN
                && std::find(indicatorTypes.begin(), indicatorTypes.end(), indicatorType) == indicatorTypes.end()) {
            indicatorTypes.push_back(indicatorType);
        }
    }
    if (indicatorTypes.empty()) {
        CalculationResult result;
        result.calculationId = foundation::utils::Uuid::generate_v4();
        result.date = context.date;
        result.dataStatus = CalculationResult::createError("技术因子缺少有效技术指标配置").dataStatus;
        result.metadata.set(technical_metadata::kEmptyReasonKey,
                            json_helper::toJsonValue("技术因子缺少有效技术指标配置"));
        result.metadata.set(technical_metadata::kTechnicalConfigModeKey,
                            json_helper::toJsonValue(static_cast<int>(technicalConfigMode)));
        return result;
    }

    const TechnicalConfigMode technicalResolvedConfigMode = technicalConfigMode;

    const TechnicalCombinationMode combinationMode = technical.technicalCombinationMode;
    const TechnicalPriceIndicatorSpec priceIndicator = configurable_factor_detail::technicalPriceIndicatorSpec(technical.technicalPriceType);
    const factor::bridge::FieldKey* priceFieldKey = priceIndicator.common.fieldKey;
    const std::string priceFieldName = priceFieldKey ? priceFieldKey->c_str() : std::string();
    const std::string highFieldName = factor::bridge::MarketBarFieldKeys::HIGH.c_str();
    const std::string lowFieldName = factor::bridge::MarketBarFieldKeys::LOW.c_str();
    const std::string volumeFieldName = factor::bridge::MarketBarFieldKeys::VOLUME.c_str();
    const int rsiWindow = (std::max)(2, technical.rsiWindow);
    const int maWindow = (std::max)(2, technical.maWindow);
    const int emaWindow = (std::max)(2, technical.emaWindow);
    const int bollWindow = (std::max)(2, technical.bollWindow);
    const double bollStdDev = technical.bollStdDev > 0.0 ? technical.bollStdDev : 2.0;
    const int kdjWindow = (std::max)(2, technical.kdjWindow);
    const int kdjKPeriod = (std::max)(2, technical.kdjKPeriod);
    const int kdjDPeriod = (std::max)(2, technical.kdjDPeriod);
    const int atrWindow = (std::max)(2, technical.atrWindow);
    const int macdFastPeriod = (std::max)(2, technical.macdFastPeriod);
    const int macdSlowPeriod = (std::max)(macdFastPeriod + 1, technical.macdSlowPeriod);
    const int macdSignalPeriod = (std::max)(2, technical.macdSignalPeriod);
    const int obvWindow = (std::max)(2, technical.obvWindow);
    const int vwapWindow = (std::max)(2, technical.vwapWindow);
    const int volumeRatioWindow = (std::max)(2, technical.volumeRatioWindow);
    const int turnoverStabilityWindow = (std::max)(2, technical.turnoverStabilityWindow);
    const auto symbols = effectiveSymbols(context);
    CalculationContext technicalContext = context;
    technicalContext.symbols = symbols;
    const bool useLocalBatchCache = context.historicalView
        && (!activeBatchComputationCache || activeBatchComputationCache->historicalView != context.historicalView);
    const bool needHighLowSeries = std::any_of(indicatorTypes.begin(), indicatorTypes.end(), technicalIndicatorUsesHighLow);
    const bool needVolumeSeries = std::any_of(indicatorTypes.begin(), indicatorTypes.end(), technicalIndicatorUsesVolume);
    const bool needPriceSeries = std::any_of(indicatorTypes.begin(), indicatorTypes.end(), technicalIndicatorUsesPriceField);
    if (needPriceSeries && (!priceIndicator.common.hasResolvedSource() || priceIndicator.common.sourceTable != SourceTable::DAILY_BAR)) {
        CalculationResult result;
        result.calculationId = foundation::utils::Uuid::generate_v4();
        result.date = context.date;
        result.dataStatus = CalculationResult::createError("技术因子缺少合法价格字段配置").dataStatus;
        result.metadata.set(technical_metadata::kEmptyReasonKey,
                            json_helper::toJsonValue("技术因子缺少合法价格字段配置"));
        setTechnicalIndicatorContextMetadata(result, technicalResolvedConfigMode, indicatorTypes);
        return result;
    }
    const LiquidityIndicatorSpec turnoverIndicator = liquidityIndicatorSpec(technical.turnoverStabilityMetric);
    const factor::bridge::FieldKey* turnoverMetricFieldKey = turnoverIndicator.common.fieldKey;
    const bool needTurnoverSeries = std::any_of(indicatorTypes.begin(), indicatorTypes.end(), technicalIndicatorUsesTurnoverMetric);
    if (needTurnoverSeries && (!turnoverIndicator.common.hasResolvedSource() || turnoverIndicator.common.sourceTable != SourceTable::DAILY_BAR)) {
        CalculationResult result;
        result.calculationId = foundation::utils::Uuid::generate_v4();
        result.date = context.date;
        result.dataStatus = CalculationResult::createError("技术因子缺少合法换手稳定性字段配置").dataStatus;
        result.metadata.set(technical_metadata::kEmptyReasonKey,
                            json_helper::toJsonValue("技术因子缺少合法换手稳定性字段配置"));
        setTechnicalIndicatorContextMetadata(result, technicalResolvedConfigMode, indicatorTypes);
        return result;
    }
    const std::string turnoverFieldName = turnoverMetricFieldKey ? turnoverMetricFieldKey->c_str() : std::string();

    auto calculateTechnicalBody = [&]() -> CalculationResult {
        const size_t closeWindow = static_cast<size_t>((std::max)({
            rsiWindow + 1,
            maWindow,
            emaWindow,
            bollWindow,
            kdjWindow + 1,
            atrWindow + 1,
            macdSlowPeriod + macdSignalPeriod + 5,
            obvWindow + 1,
            vwapWindow + 1,
            volumeRatioWindow + 1
        }));
        const size_t highLowHistoryWindow = static_cast<size_t>((std::max)({kdjWindow + 1, atrWindow + 1, static_cast<int>(closeWindow)}));
        const size_t volumeHistoryWindow = static_cast<size_t>((std::max)({obvWindow + 1, vwapWindow + 1, volumeRatioWindow + 1, turnoverStabilityWindow}));
        const size_t maxWindow = (std::max)({closeWindow, highLowHistoryWindow, volumeHistoryWindow, static_cast<size_t>(turnoverStabilityWindow)});
        const size_t technicalLookbackWindow = maxWindow + 5;

        std::vector<std::string> runtimeSymbols = symbols;
        technicalContext.symbols = runtimeSymbols;

        if (runtimeSymbols.empty()) {
            CalculationResult result;
            result.calculationId = foundation::utils::Uuid::generate_v4();
            result.date = context.date;
            result.metadata.set(technical_metadata::kEmptyReasonKey,
                                json_helper::toJsonValue("技术因子缺少可用标的"));
            setTechnicalIndicatorContextMetadata(result, technicalResolvedConfigMode, indicatorTypes);
            return result;
        }

        std::vector<std::string> requestedFields;
        requestedFields.reserve(5);
        std::unordered_set<std::string> requestedFieldSet;
        const auto appendField = [&](const std::string& fieldName) {
            if (requestedFieldSet.insert(fieldName).second) {
                requestedFields.push_back(fieldName);
            }
        };
        if (needPriceSeries) {
            appendField(priceFieldName);
        }
        if (needHighLowSeries) {
            appendField(highFieldName);
            appendField(lowFieldName);
        }
        if (needVolumeSeries) {
            appendField(volumeFieldName);
        }
        if (needTurnoverSeries) {
            appendField(turnoverFieldName);
        }

        return executeWithCommonParams(
            technicalContext,
            common,
            [this, &technicalContext, &common, &requestedFields]() {
                return resolveCommonEffectiveDateForFields(
                    technicalContext,
                    common,
                    requestedFields,
                    CommonFieldRequirementMode::AnyField);
            },
            [&, runtimeSymbols](const CommonRuntimeState& runtime, CalculationResult& result) {
                technicalContext.date = runtime.effectiveDate;

                const auto batchData = technicalContext.historicalView->getBatchTimeSeries(
                    runtimeSymbols,
                    technicalContext.date,
                    static_cast<int>(technicalLookbackWindow),
                    requestedFields);

                const auto findSeriesMap = [&](const std::string& fieldName) -> const std::unordered_map<std::string, std::vector<double>>* {
                    const auto fieldIt = batchData.find(fieldName);
                    if (fieldIt == batchData.end() || fieldIt->second.empty()) {
                        return nullptr;
                    }
                    return &fieldIt->second;
                };

                const auto* closesBySymbol = needPriceSeries ? findSeriesMap(priceFieldName) : nullptr;
                if (needPriceSeries && !closesBySymbol) {
                    result.metadata.set(technical_metadata::kEmptyReasonKey,
                                        json_helper::toJsonValue("技术因子没有可用价格数据"));
                    return;
                }

                const auto* highsBySymbol = needHighLowSeries ? findSeriesMap(highFieldName) : nullptr;
                const auto* lowsBySymbol = needHighLowSeries ? findSeriesMap(lowFieldName) : nullptr;
                const auto* volumesBySymbol = needVolumeSeries ? findSeriesMap(volumeFieldName) : nullptr;
                const auto* turnoverSeriesBySymbol = needTurnoverSeries ? findSeriesMap(turnoverFieldName) : nullptr;

                std::unordered_map<std::string, std::vector<double>> scoresBySymbol;
                scoresBySymbol.reserve(symbols.size());

                for (const TechnicalIndicator indicatorType : indicatorTypes) {
                    std::unordered_map<std::string, double> indicatorScores;
                    switch (indicatorType) {
                    case TechnicalIndicator::RSI:
                        indicatorScores = batchCalculateRsi(*closesBySymbol, rsiWindow);
                        break;
                    case TechnicalIndicator::MACD:
                        indicatorScores = batchCalculateMacd(*closesBySymbol, macdFastPeriod, macdSlowPeriod, macdSignalPeriod);
                        break;
                    case TechnicalIndicator::MA:
                        indicatorScores = batchCalculateMa(*closesBySymbol, maWindow);
                        break;
                    case TechnicalIndicator::EMA:
                        indicatorScores = batchCalculateEma(*closesBySymbol, emaWindow);
                        break;
                    case TechnicalIndicator::BOLL:
                        indicatorScores = batchCalculateBoll(*closesBySymbol, bollWindow, bollStdDev);
                        break;
                    case TechnicalIndicator::KDJ:
                        if (highsBySymbol && lowsBySymbol) {
                            indicatorScores = batchCalculateKdj(*highsBySymbol, *lowsBySymbol, *closesBySymbol, kdjWindow, kdjKPeriod, kdjDPeriod);
                        }
                        break;
                    case TechnicalIndicator::ATR:
                        if (highsBySymbol && lowsBySymbol) {
                            indicatorScores = batchCalculateAtr(*highsBySymbol, *lowsBySymbol, *closesBySymbol, atrWindow);
                        }
                        break;
                    case TechnicalIndicator::OBV:
                        if (volumesBySymbol) {
                            indicatorScores = batchCalculateObv(*closesBySymbol, *volumesBySymbol, obvWindow);
                        }
                        break;
                    case TechnicalIndicator::VWAP:
                        if (volumesBySymbol) {
                            indicatorScores = batchCalculateVwap(*closesBySymbol, *volumesBySymbol);
                        }
                        break;
                    case TechnicalIndicator::VOLUME_RATIO:
                        if (volumesBySymbol) {
                            indicatorScores = batchCalculateVolumeRatio(*volumesBySymbol, volumeRatioWindow);
                        }
                        break;
                    case TechnicalIndicator::TURNOVER_STABILITY:
                        if (turnoverSeriesBySymbol) {
                            indicatorScores = batchCalculateTurnoverStability(*turnoverSeriesBySymbol, turnoverStabilityWindow);
                        }
                        break;
                    default:
                        break;
                    }
                    for (const auto& [symbol, score] : indicatorScores) {
                        if (std::isfinite(score)) {
                            scoresBySymbol[symbol].push_back(score);
                        }
                    }
                }

                for (const auto& symbol : runtimeSymbols) {
                    const auto scoreIt = scoresBySymbol.find(symbol);
                    if (scoreIt == scoresBySymbol.end() || scoreIt->second.empty()) {
                        continue;
                    }

                    double combinedScore = safeMean(scoreIt->second);
                    if (combinationMode == TechnicalCombinationMode::NormalizedAverage && scoreIt->second.size() > 1) {
                        double magnitude = 0.0;
                        for (double score : scoreIt->second) {
                            magnitude += std::abs(score);
                        }
                        if (magnitude > 1e-12) {
                            combinedScore = combinedScore / (magnitude / static_cast<double>(scoreIt->second.size()));
                        }
                    }
                    combinedScore = std::clamp(combinedScore, -1.0, 1.0);
                    if (std::isfinite(combinedScore)) {
                        result.values[symbol] = combinedScore;
                    }
                }

                if (result.values.empty()) {
                    result.metadata.set(technical_metadata::kEmptyReasonKey,
                                        json_helper::toJsonValue("技术因子没有可用价格数据"));
                }
            },
            [](const CommonRuntimeState&, CalculationResult&) {},
            [&](const CommonRuntimeState&, CalculationResult& result) {
                result.metadata.set(technical_metadata::kIndicatorTypeKey,
                                    json_helper::toJsonValue(static_cast<int>(indicatorTypes.front())));
                setTechnicalIndicatorContextMetadata(result, technicalResolvedConfigMode, indicatorTypes);
                result.metadata.set(technical_metadata::kPriceTypeKey,
                                    json_helper::toJsonValue(static_cast<int>(technical.technicalPriceType)));
                result.metadata.set(technical_metadata::kPriceSourceTableKey,
                                    json_helper::toJsonValue(static_cast<int>(priceIndicator.common.sourceTable)));
                result.metadata.set(technical_metadata::kActualPriceTypeKey,
                                    json_helper::toJsonValue(static_cast<int>(priceIndicator.priceType)));
                result.metadata.set(technical_metadata::kPriceFieldDerivedKey, json_helper::toJsonValue(false));
                result.metadata.set(technical_metadata::kUseVolumeKey, json_helper::toJsonValue(technical.useVolume));
                result.metadata.set(technical_metadata::kWindowKey, json_helper::toJsonValue(rsiWindow));
                result.metadata.set(technical_metadata::kTechnicalCombinationModeKey,
                                    json_helper::toJsonValue(static_cast<int>(combinationMode)));
                result.metadata.set(technical_metadata::kMaWindowKey, json_helper::toJsonValue(maWindow));
                result.metadata.set(technical_metadata::kEmaWindowKey, json_helper::toJsonValue(emaWindow));
                result.metadata.set(technical_metadata::kBollWindowKey, json_helper::toJsonValue(bollWindow));
                result.metadata.set(technical_metadata::kBollStdDevKey, json_helper::toJsonValue(bollStdDev));
                result.metadata.set(technical_metadata::kKdjWindowKey, json_helper::toJsonValue(kdjWindow));
                result.metadata.set(technical_metadata::kKdjKPeriodKey, json_helper::toJsonValue(kdjKPeriod));
                result.metadata.set(technical_metadata::kKdjDPeriodKey, json_helper::toJsonValue(kdjDPeriod));
                result.metadata.set(technical_metadata::kAtrWindowKey, json_helper::toJsonValue(atrWindow));
                result.metadata.set(technical_metadata::kMacdFastPeriodKey, json_helper::toJsonValue(macdFastPeriod));
                result.metadata.set(technical_metadata::kMacdSlowPeriodKey, json_helper::toJsonValue(macdSlowPeriod));
                result.metadata.set(technical_metadata::kMacdSignalPeriodKey,
                                    json_helper::toJsonValue(macdSignalPeriod));
                result.metadata.set(technical_metadata::kObvWindowKey, json_helper::toJsonValue(obvWindow));
                result.metadata.set(technical_metadata::kVwapWindowKey, json_helper::toJsonValue(vwapWindow));
                result.metadata.set(technical_metadata::kVolumeRatioWindowKey,
                                    json_helper::toJsonValue(volumeRatioWindow));
                result.metadata.set(technical_metadata::kTurnoverStabilityWindowKey,
                                    json_helper::toJsonValue(turnoverStabilityWindow));
                result.metadata.set(technical_metadata::kTurnoverStabilityMetricKey,
                                    json_helper::toJsonValue(static_cast<int>(technical.turnoverStabilityMetric)));
                result.metadata.set(technical_metadata::kTurnoverStabilitySourceTableKey,
                                    json_helper::toJsonValue(static_cast<int>(turnoverIndicator.common.sourceTable)));
            });
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.historicalView = context.historicalView;
        BatchComputationCacheScope scope(cache);
        return calculateTechnicalBody();
    }
    return calculateTechnicalBody();
}

} // namespace factor