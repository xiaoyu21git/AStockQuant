#include "domain/factor/include/ConfigurableFactorDetail.h"

#include <QDate>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_set>

namespace factor {

using namespace configurable_factor_detail;

namespace {

template <typename LatestFinancialSeriesResolver>
std::unordered_map<std::string, double> computeGrowthYoYScoreMap(
    LatestFinancialSeriesResolver&& latestFinancialSeries,
    const CalculationContext& context,
    const QString& effectiveDate,
    const QString& field)
{
    std::unordered_map<std::string, double> scores;
    const auto seriesMap = latestFinancialSeries(context, field, effectiveDate, 2);
    for (const auto& [symbol, values] : seriesMap) {
        if (values.size() < 2) {
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
    const QString& effectiveDate,
    const QString& field)
{
    std::unordered_map<std::string, double> scores;
    const auto seriesMap = latestFinancialSeries(context, field, effectiveDate, 2);
    for (const auto& [symbol, values] : seriesMap) {
        if (values.size() < 2) {
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
std::unordered_map<std::string, double> computeGrowthSueProxyScoreMap(
    LatestFinancialSeriesResolver&& latestFinancialSeries,
    const CalculationContext& context,
    const QString& effectiveDate)
{
    std::unordered_map<std::string, double> scores;
    const auto seriesMap = latestFinancialSeries(context, QString(factor::bridge::FinancialFieldKeys::EPS), effectiveDate, 5);
    for (const auto& [symbol, values] : seriesMap) {
        if (values.size() < 2) {
            continue;
        }

        std::vector<double> changes;
        changes.reserve(values.size() - 1);
        for (size_t index = 0; index + 1 < values.size(); ++index) {
            changes.push_back(values[index] - values[index + 1]);
        }

        if (changes.empty()) {
            continue;
        }

        const double currentChange = changes.front();
        if (changes.size() < 2) {
            if (std::isfinite(currentChange)) {
                scores[symbol] = currentChange;
            }
            continue;
        }

        double historyMean = 0.0;
        for (size_t index = 1; index < changes.size(); ++index) {
            historyMean += changes[index];
        }
        historyMean /= static_cast<double>(changes.size() - 1);

        double variance = 0.0;
        for (size_t index = 1; index < changes.size(); ++index) {
            const double diff = changes[index] - historyMean;
            variance += diff * diff;
        }
        variance /= static_cast<double>(changes.size() - 1);

        const double stdDev = std::sqrt(std::max(variance, 0.0));
        const double sueScore = stdDev > 1e-12 ? (currentChange - historyMean) / stdDev : currentChange;
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
        if (ranked.size() == 1) {
            scores[ranked.front().first] = 1.0;
            return;
        }
        for (size_t index = 0; index < ranked.size(); ++index) {
            scores[ranked[index].first] = standardization == StandardizationMethod::Rank
                ? static_cast<double>(index + 1)
                : static_cast<double>(index) / static_cast<double>(ranked.size() - 1);
        }
        return;
    }

    std::vector<double> values;
    values.reserve(scores.size());
    for (const auto& [symbol, value] : scores) {
        Q_UNUSED(symbol);
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
                Q_UNUSED(symbol);
                value = (value - mean) / stdev;
            }
        }
    } else if (standardization == StandardizationMethod::MinMax) {
        const auto [minIt, maxIt] = std::minmax_element(values.begin(), values.end());
        const double range = *maxIt - *minIt;
        if (range > 1e-12) {
            for (auto& [symbol, value] : scores) {
                Q_UNUSED(symbol);
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
        return createHistoricalViewRuntimeError(context, QStringLiteral("成长因子配置必须显式提供等长的 growthMetrics 和 growthWeights").toStdString());
    }

    const size_t pairCount = selectedMetrics.size();
    const DataFrequency frequency = common.frequency;
    const StandardizationMethod standardization = common.standardization;
    std::unordered_set<std::string> seenMetrics;

    struct GrowthMetricSelection {
        GrowthIndicatorSpec indicator;
        double weight{0.0};
        QString field;
    };

    std::vector<GrowthMetricSelection> selections;
    selections.reserve(pairCount);

    for (size_t index = 0; index < pairCount; ++index) {
        const GrowthMetric metric = selectedMetrics[index];
        const double weight = selectedWeights[index];
        if (metric == GrowthMetric::UNKNOWN || !std::isfinite(weight) || weight < 0.0) {
            return createHistoricalViewRuntimeError(context, QStringLiteral("成长因子配置包含非法指标或权重").toStdString());
        }
        const std::string metricKey = std::to_string(static_cast<int>(metric));
        if (seenMetrics.find(metricKey) != seenMetrics.end()) {
            return createHistoricalViewRuntimeError(context, QStringLiteral("成长因子配置不允许重复指标").toStdString());
        }
        seenMetrics.insert(metricKey);

        const GrowthIndicatorSpec indicatorSpec = growthIndicatorSpec(metric);
        if (!indicatorSpec.common.hasResolvedSource() || indicatorSpec.common.sourceTable != SourceTable::FINANCIAL_INDICATOR) {
            return createHistoricalViewRuntimeError(context, QStringLiteral("成长因子配置包含不支持的指标").toStdString());
        }
        const QString field = indicatorSpec.common.fieldKey->toQString();
        selections.push_back({indicatorSpec, weight, field});
    }

    QStringList dateResolutionFields;
    dateResolutionFields.reserve(static_cast<int>(selections.size()));
    for (const auto& selection : selections) {
        dateResolutionFields.append(selection.field);
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
            effectiveContext.date = runtime.effectiveDate.toStdString();
            auto latestFinancialSeriesResolver = [this](const CalculationContext& queryContext,
                                                        const QString& field,
                                                        const QString& date,
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
                    metricScores = computeGrowthSueProxyScoreMap(latestFinancialSeriesResolver, effectiveContext, runtime.effectiveDate);
                } else {
                    const QString errorMessage = QStringLiteral("成长因子配置包含不支持的指标");
                    result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
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
                result.metadata.set("emptyReason", json_helper::toJsonValue(QStringLiteral("成长因子没有可用财务数据").toStdString()));
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