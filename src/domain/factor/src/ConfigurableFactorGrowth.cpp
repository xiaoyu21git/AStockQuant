#include "domain/factor/include/ConfigurableFactorDetail.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <numeric>
#include <unordered_set>

namespace factor {

using namespace configurable_factor_detail;

namespace {

template <typename LatestFinancialSeriesResolver>
std::unordered_map<std::string, double> computeGrowthYoYScoreMap(
    LatestFinancialSeriesResolver&& latestFinancialSeries,
    const CalculationContext& context,
    const std::string& effectiveDate,
    const std::string& field)
{
    std::unordered_map<std::string, double> scores;
    const auto seriesMap = latestFinancialSeries(
        context,
        field,
        effectiveDate,
        ASTOCK_CONFIGURABLE_GROWTH_BASIC_SERIES_POINTS);
    for (const auto& [symbol, values] : seriesMap) {
        if (values.size() < ASTOCK_CONFIGURABLE_GROWTH_BASIC_SERIES_POINTS) {
            continue;
        }

        const double previousValue = values[1];
        if (std::abs(previousValue) < 1e-12) {
            continue;
        }

        const double growth = safeRatio(values[0] - values[1], std::abs(previousValue));
        if (std::isfinite(growth)) {
            scores[symbol] = growth;
        }
    }
    return scores;
}

template <typename LatestFinancialSeriesResolver>
std::unordered_map<std::string, double> computeGrowthDifferenceScoreMap(
    LatestFinancialSeriesResolver&& latestFinancialSeries,
    const CalculationContext& context,
    const std::string& effectiveDate,
    const std::string& field)
{
    std::unordered_map<std::string, double> scores;
    const auto seriesMap = latestFinancialSeries(
        context,
        field,
        effectiveDate,
        ASTOCK_CONFIGURABLE_GROWTH_BASIC_SERIES_POINTS);
    for (const auto& [symbol, values] : seriesMap) {
        if (values.size() < ASTOCK_CONFIGURABLE_GROWTH_BASIC_SERIES_POINTS) {
            continue;
        }

        const double delta = values[0] - values[1];
        if (std::isfinite(delta)) {
            scores[symbol] = delta;
        }
    }
    return scores;
}

template <typename LatestFinancialSeriesResolver>
std::unordered_map<std::string, double> computeGrowthSueScoreMap(
    LatestFinancialSeriesResolver&& latestFinancialSeries,
    const CalculationContext& context,
    const std::string& effectiveDate)
{
    std::unordered_map<std::string, double> scores;
    const auto seriesMap = latestFinancialSeries(
        context,
        std::string(factor::bridge::FinancialFieldKeys::EPS.c_str()),
        effectiveDate,
        ASTOCK_CONFIGURABLE_GROWTH_SUE_SERIES_POINTS);
    for (const auto& [symbol, values] : seriesMap) {
        if (values.size() < ASTOCK_CONFIGURABLE_GROWTH_SUE_SERIES_POINTS) {
            continue;
        }

        std::vector<double> chronologicalValues(values.rbegin(), values.rend());
        std::vector<double> historicalSurprises;
        historicalSurprises.reserve(ASTOCK_CONFIGURABLE_GROWTH_SUE_HISTORY_COUNT);
        for (size_t index = 0; index < ASTOCK_CONFIGURABLE_GROWTH_SUE_HISTORY_COUNT; ++index) {
            const double seasonalSurprise = chronologicalValues[index + ASTOCK_CONFIGURABLE_GROWTH_SUE_SEASONAL_LAG]
                - chronologicalValues[index];
            if (!std::isfinite(seasonalSurprise)) {
                historicalSurprises.clear();
                break;
            }
            historicalSurprises.push_back(seasonalSurprise);
        }
        if (historicalSurprises.size() != ASTOCK_CONFIGURABLE_GROWTH_SUE_HISTORY_COUNT) {
            continue;
        }

        const double currentSurprise = chronologicalValues[ASTOCK_CONFIGURABLE_GROWTH_SUE_SERIES_POINTS - 1]
            - chronologicalValues[ASTOCK_CONFIGURABLE_GROWTH_SUE_HISTORY_COUNT];
        if (!std::isfinite(currentSurprise)) {
            continue;
        }

        const double historyMean = std::accumulate(
            historicalSurprises.begin(),
            historicalSurprises.end(),
            0.0) / static_cast<double>(historicalSurprises.size());
        double variance = 0.0;
        for (const double surprise : historicalSurprises) {
            const double delta = surprise - historyMean;
            variance += delta * delta;
        }
        const double stdDev = std::sqrt(variance / static_cast<double>(historicalSurprises.size()));
        if (stdDev <= 1e-12) {
            continue;
        }

        const double sueScore = (currentSurprise - historyMean) / stdDev;
        if (std::isfinite(sueScore)) {
            scores[symbol] = sueScore;
        }
    }
    return scores;
}

void normalizeGrowthScoreMap(StandardizationMethod standardization, std::unordered_map<std::string, double>& scores)
{
    if (scores.empty()) {
        return;
    }

    if (standardization == StandardizationMethod::Percentile || standardization == StandardizationMethod::Rank) {
        std::vector<std::pair<std::string, double>> ranked(scores.begin(), scores.end());
        std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
            return left.second < right.second;
        });
        const double denominator = static_cast<double>(ranked.size());
        for (size_t index = 0; index < ranked.size();) {
            size_t groupEnd = index + 1;
            while (groupEnd < ranked.size() && ranked[groupEnd].second == ranked[index].second) {
                ++groupEnd;
            }

            const double precedingFraction = static_cast<double>(index) / denominator;
            for (size_t groupIndex = index; groupIndex < groupEnd; ++groupIndex) {
                scores[ranked[groupIndex].first] = precedingFraction;
            }
            index = groupEnd;
        }
        return;
    }

    std::vector<double> values;
    values.reserve(scores.size());
    for (const auto& [symbol, value] : scores) {
        if (std::isfinite(value)) {
            values.push_back(value);
        }
    }

    if (values.empty()) {
        return;
    }

    if (standardization == StandardizationMethod::ZScore) {
        const double mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
        double variance = 0.0;
        for (double value : values) {
            const double delta = value - mean;
            variance += delta * delta;
        }
        const double stdev = std::sqrt(variance / static_cast<double>(values.size()));
        if (stdev > 1e-12) {
            for (auto& [symbol, value] : scores) {
                value = (value - mean) / stdev;
            }
        }
    } else if (standardization == StandardizationMethod::MinMax) {
        const auto [minIt, maxIt] = std::minmax_element(values.begin(), values.end());
        const double range = *maxIt - *minIt;
        if (range > 1e-12) {
            for (auto& [symbol, value] : scores) {
                value = (value - *minIt) / range;
            }
        }
    }
}

} // namespace

CalculationResult ConfigurableFactorBase::calculateGrowth(const CalculationContext& context) const
{
    const CommonParams& common = commonParams_;
    const GrowthParams& growth = growthParams();
    const std::vector<GrowthMetric>& selectedMetrics = growth.growthMetrics;
    const std::vector<double>& selectedWeights = growth.growthWeights;
    if (selectedMetrics.empty() || selectedWeights.empty() || selectedMetrics.size() != selectedWeights.size()) {
        return createHistoricalViewRuntimeError(context, "成长因子配置必须显式提供等长的 growthMetrics 和 growthWeights");
    }

    const size_t pairCount = selectedMetrics.size();
    const DataFrequency frequency = common.frequency;
    const StandardizationMethod standardization = common.standardization;
    std::unordered_set<std::string> seenMetrics;

    struct GrowthMetricSelection {
        GrowthIndicatorSpec indicator;
        double weight{0.0};
        std::string field;
    };

    std::vector<GrowthMetricSelection> selections;
    selections.reserve(pairCount);

    for (size_t index = 0; index < pairCount; ++index) {
        const GrowthMetric metric = selectedMetrics[index];
        const double weight = selectedWeights[index];
        if (metric == GrowthMetric::UNKNOWN || !std::isfinite(weight) || weight < 0.0) {
            return createHistoricalViewRuntimeError(context, "成长因子配置包含非法指标或权重");
        }
        const std::string metricKey = std::to_string(static_cast<int>(metric));
        if (seenMetrics.find(metricKey) != seenMetrics.end()) {
            return createHistoricalViewRuntimeError(context, "成长因子配置不允许重复指标");
        }
        seenMetrics.insert(metricKey);

        const GrowthIndicatorSpec indicatorSpec = growthIndicatorSpec(metric);
        if (!indicatorSpec.common.hasResolvedSource() || indicatorSpec.common.sourceTable != SourceTable::FINANCIAL_INDICATOR) {
            return createHistoricalViewRuntimeError(context, "成长因子配置包含不支持的指标");
        }
        const std::string field = std::string(indicatorSpec.common.fieldKey->c_str());
        selections.push_back({indicatorSpec, weight, field});
    }

    std::vector<std::string> dateResolutionFields;
    dateResolutionFields.reserve(selections.size());
    for (const auto& selection : selections) {
        dateResolutionFields.push_back(selection.field);
    }

    return executeWithCommonParams(
        context,
        common,
        [this, &context, &common, &dateResolutionFields]() {
            return resolveCommonEffectiveDateForFields(
                context,
                common,
                dateResolutionFields,
                CommonFieldRequirementMode::AllFields);
        },
        [this, &context, &selections, standardization](const CommonRuntimeState& runtime, CalculationResult& result) {
            CalculationContext effectiveContext = context;
            effectiveContext.date = runtime.effectiveDate;
            auto latestFinancialSeriesResolver = [this](const CalculationContext& queryContext,
                                                        const std::string& field,
                                                        const std::string& date,
                                                        int limit) {
                return latestFinancialSeries(queryContext, field, date, limit);
            };

            std::unordered_map<std::string, double> combinedScores;
            std::unordered_map<std::string, double> activeWeightSums;

            for (const auto& selection : selections) {
                if (selection.weight == 0.0) {
                    continue;
                }

                std::unordered_map<std::string, double> metricScores;
                if (selection.indicator.metric == GrowthMetric::REVENUE_GROWTH) {
                    metricScores = computeGrowthYoYScoreMap(latestFinancialSeriesResolver, effectiveContext, runtime.effectiveDate, selection.field);
                } else if (selection.indicator.metric == GrowthMetric::NET_PROFIT_GROWTH) {
                    metricScores = computeGrowthYoYScoreMap(latestFinancialSeriesResolver, effectiveContext, runtime.effectiveDate, selection.field);
                } else if (selection.indicator.metric == GrowthMetric::DELTA_ROE) {
                    metricScores = computeGrowthDifferenceScoreMap(latestFinancialSeriesResolver, effectiveContext, runtime.effectiveDate, selection.field);
                } else if (selection.indicator.metric == GrowthMetric::SUE) {
                    metricScores = computeGrowthSueScoreMap(latestFinancialSeriesResolver, effectiveContext, runtime.effectiveDate);
                } else {
                    const std::string errorMessage = "成长因子配置包含不支持的指标";
                    result.dataStatus = CalculationResult::createError(errorMessage).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(errorMessage));
                    return;
                }

                normalizeGrowthScoreMap(standardization, metricScores);

                for (const auto& [symbol, score] : metricScores) {
                    if (!std::isfinite(score)) {
                        continue;
                    }
                    combinedScores[symbol] += score * selection.weight;
                    activeWeightSums[symbol] += selection.weight;
                }
            }

            if (combinedScores.empty()) {
                result.metadata.set("emptyReason", json_helper::toJsonValue("成长因子没有可用财务数据"));
                return;
            }

            for (const auto& [symbol, weightedScore] : combinedScores) {
                const double weightSum = activeWeightSums[symbol];
                if (weightSum > 1e-12) {
                    result.values[symbol] = weightedScore / weightSum;
                }
            }
        },
        [](const CommonRuntimeState&, CalculationResult&) {},
        [&selectedMetrics](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set(
                "metric",
                json_helper::toJsonValue(selectedMetrics.empty() ? static_cast<int>(GrowthMetric::UNKNOWN) : static_cast<int>(selectedMetrics.front())));
            result.metadata.set("metrics", growthMetricArrayJson(selectedMetrics));
            result.metadata.set("metricSourceTable", json_helper::toJsonValue(static_cast<int>(SourceTable::FINANCIAL_INDICATOR)));
            result.metadata.set("dataMode", json_helper::toJsonValue(static_cast<int>(ConfigurableDataMode::FinancialSeriesDirect)));
        });
}

} // namespace factor