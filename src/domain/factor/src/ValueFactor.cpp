#include "domain/factor/include/ValueFactor.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include "domain/factor/include/HistoricalView.h"

#include <QDate>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_set>

namespace factor {

namespace {

QString normalizedMetric(const std::string& metric)
{
    const QString rawMetric = QString::fromStdString(metric).trimmed();
    const QString normalized = rawMetric.toLower();
    if (normalized == QStringLiteral("bp")) {
        return QStringLiteral("bp");
    }
    if (normalized == QStringLiteral("ep")) {
        return QStringLiteral("ep");
    }
    if (normalized == QStringLiteral("cf_p")) {
        return QStringLiteral("cf_p");
    }
    if (normalized == QStringLiteral("dividend_yield")) {
        return QStringLiteral("dividend_yield");
    }
    return {};
}

QString normalizedFrequency(const std::string& frequency)
{
    const QString normalized = QString::fromStdString(frequency).trimmed().toLower();
    if (normalized == QStringLiteral("weekly") || normalized == QStringLiteral("周频")) {
        return QStringLiteral("weekly");
    }
    if (normalized == QStringLiteral("monthly") || normalized == QStringLiteral("月频")) {
        return QStringLiteral("monthly");
    }
    return QStringLiteral("daily");
}

QString normalizedStandardization(const std::string& standardization)
{
    const QString normalized = QString::fromStdString(standardization).trimmed().toLower();
    if (normalized == QStringLiteral("zscore") || normalized == QStringLiteral("z_score")
            || normalized == QStringLiteral("z-score") || normalized == QStringLiteral("z score")) {
        return QStringLiteral("zscore");
    }
    if (normalized == QStringLiteral("minmax") || normalized == QStringLiteral("min_max")
            || normalized == QStringLiteral("min-max") || normalized == QStringLiteral("min max")) {
        return QStringLiteral("minmax");
    }
    if (normalized == QStringLiteral("percentile") || normalized == QStringLiteral("rank")) {
        return QStringLiteral("percentile");
    }
    return QStringLiteral("none");
}

double percentileValue(std::vector<double> values, double quantile)
{
    if (values.empty()) {
        return 0.0;
    }
    quantile = (std::max)(0.0, (std::min)(1.0, quantile));
    const double position = quantile * static_cast<double>(values.size() - 1);
    const size_t lower = static_cast<size_t>(std::floor(position));
    const size_t upper = static_cast<size_t>(std::ceil(position));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(lower), values.end());
    const double lowValue = values[lower];
    if (upper == lower) {
        return lowValue;
    }
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(upper), values.end());
    const double highValue = values[upper];
    return lowValue + (highValue - lowValue) * (position - static_cast<double>(lower));
}

}

ValueFactor::ValueFactor() {
    factorType_ = "价值因子";
}

CalculationResult ValueFactor::calculate(const CalculationContext& context) {
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(
            context,
            QStringLiteral("已移除价值因子运行期数据库取数路径，请由引擎提供 HistoricalView").toStdString());
    }

    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用缓存数据集";

    if (isHistoricalViewRuntime(context) && params_.industryNeutral) {
        return createHistoricalViewRuntimeError(
            context,
            QStringLiteral("价值因子 HistoricalView 回测已禁止行业中性化数据库回退，请由引擎提供中性化后的输入或关闭 industryNeutral")
                .toStdString());
    }

    const QStringList selectedMetrics = [&]() {
        QStringList metrics;
        for (const auto& metric : params_.valuationMetrics) {
            const QString normalized = normalizedMetric(metric);
            if (!normalized.isEmpty() && !metrics.contains(normalized)) {
                metrics.append(normalized);
            }
        }
        if (metrics.isEmpty()) {
            const QString fallback = normalizedMetric(params_.valuationType);
            if (!fallback.isEmpty()) {
                metrics.append(fallback);
            }
        }
        return metrics;
    }();

    if (selectedMetrics.isEmpty()) {
        const QString errorMessage = QString::fromUtf8("价值因子未配置有效的 valuationMetrics");
        result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
        result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
        return result;
    }

    auto selectedFieldForMetric = [](const QString& metric) -> QString {
        if (metric == QStringLiteral("ep")) {
            return QStringLiteral("pe_ratio");
        }
        if (metric == QStringLiteral("bp")) {
            return QStringLiteral("pb_ratio");
        }
        if (metric == QStringLiteral("dividend_yield")) {
            return QStringLiteral("dividend_yield");
        }
        return {};
    };

    auto metricWeight = [&](const QString& metric) -> double {
        if (metric == QStringLiteral("bp")) {
            return params_.bpWeight;
        }
        if (metric == QStringLiteral("ep")) {
            return params_.epWeight;
        }
        if (metric == QStringLiteral("dividend_yield")) {
            return params_.dividendYieldWeight;
        }
        if (metric == QStringLiteral("cf_p")) {
            return params_.cfPWeight;
        }
        return 0.0;
    };

    auto scoreFromMetricRawValue = [](const QString& metric, double rawValue) -> double {
        if (metric == QStringLiteral("bp") || metric == QStringLiteral("ep")) {
            return 1.0 / rawValue;
        }
        if (metric == QStringLiteral("dividend_yield") || metric == QStringLiteral("cf_p")) {
            return rawValue;
        }
        return 0.0;
    };

    const QString primaryMetric = selectedMetrics.front();
    const QString primaryField = selectedFieldForMetric(primaryMetric);
    if (primaryMetric != QStringLiteral("cf_p") && primaryField.isEmpty()) {
        const QString errorMessage = QString("当前运行时暂不支持计算价值因子指标 %1").arg(primaryMetric);
        result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
        result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
        return result;
    }

    const QString frequency = normalizedFrequency(params_.frequency);
    const QString standardization = normalizedStandardization(params_.standardization);
    auto resolvePreviousAvailableDate = [&](const QString& anchorDate, const QString& requiredField) {
        if (anchorDate.isEmpty()) {
            return QString::fromStdString(context.date);
        }

        if (context.historicalView) {
            const std::vector<std::string> symbols = context.symbols.empty()
            ? context.historicalView->getAvailableSymbols(context.date)
                : context.symbols;
            for (int offset = 1; offset <= 45; ++offset) {
                const QString candidate = QDate::fromString(anchorDate, Qt::ISODate).addDays(-offset).toString(Qt::ISODate);
                if (context.historicalView->getCrossSection(candidate.toStdString(), requiredField.toStdString(), symbols).empty()) {
                    continue;
                }
                return candidate;
            }
        }

        return anchorDate;
    };

    QString effectiveDate = QString::fromStdString(context.date);
    QDate anchorDate = QDate::fromString(effectiveDate, Qt::ISODate);
    if (anchorDate.isValid()) {
        if (frequency == QStringLiteral("weekly")) {
            const int shiftToPreviousFriday = anchorDate.dayOfWeek() >= 5 ? anchorDate.dayOfWeek() - 5 : anchorDate.dayOfWeek() + 2;
            anchorDate = anchorDate.addDays(-shiftToPreviousFriday);
        } else if (frequency == QStringLiteral("monthly")) {
            anchorDate = QDate(anchorDate.year(), anchorDate.month(), 1).addDays(-1);
        }
        effectiveDate = anchorDate.toString(Qt::ISODate);
    }

    if (params_.laggedEnabled) {
        effectiveDate = resolvePreviousAvailableDate(effectiveDate, primaryMetric == QStringLiteral("cf_p") ? QStringLiteral("market_cap") : primaryField);
    }

    struct MetricContribution {
        QString metric;
        double weight{0.0};
        std::unordered_map<std::string, double> scores;
        int rawSampleCount{0};
        int invalidSampleCount{0};
    };

    std::vector<MetricContribution> metricContributions;
    metricContributions.reserve(static_cast<size_t>(selectedMetrics.size()));

    auto applyCrossSectionPostProcessing = [&]() {
        if (result.values.empty()) {
            return;
        }

        std::vector<double> finiteValues;
        finiteValues.reserve(result.values.size());
        for (const auto& [symbol, value] : result.values) {
            Q_UNUSED(symbol);
            if (std::isfinite(value)) {
                finiteValues.push_back(value);
            }
        }
        if (finiteValues.size() >= 16) {
            const double lower = percentileValue(finiteValues, 0.05);
            const double upper = percentileValue(finiteValues, 0.95);
            if (upper > lower) {
                for (auto& [symbol, value] : result.values) {
                    Q_UNUSED(symbol);
                    value = (std::max)(lower, (std::min)(upper, value));
                }
            }
        }

        auto applyPercentileScores = [&]() {
            std::vector<std::pair<std::string, double>> ranked(result.values.begin(), result.values.end());
            std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
                return left.second < right.second;
            });
            if (ranked.size() == 1) {
                result.values[ranked.front().first] = 1.0;
                return;
            }
            for (size_t index = 0; index < ranked.size(); ++index) {
                result.values[ranked[index].first] = static_cast<double>(index) / static_cast<double>(ranked.size() - 1);
            }
        };

        if (params_.usePercentile || standardization == QStringLiteral("percentile")) {
            applyPercentileScores();
            return;
        }

        std::vector<double> values;
        values.reserve(result.values.size());
        for (const auto& [symbol, value] : result.values) {
            Q_UNUSED(symbol);
            if (std::isfinite(value)) {
                values.push_back(value);
            }
        }
        if (values.empty()) {
            return;
        }

        if (standardization == QStringLiteral("zscore")) {
            const double mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
            double variance = 0.0;
            for (double value : values) {
                const double delta = value - mean;
                variance += delta * delta;
            }
            const double stdev = std::sqrt(variance / static_cast<double>(values.size()));
            if (stdev > 1e-12) {
                for (auto& [symbol, value] : result.values) {
                    Q_UNUSED(symbol);
                    value = (value - mean) / stdev;
                }
            }
            return;
        }

        if (standardization == QStringLiteral("minmax")) {
            const auto [minIt, maxIt] = std::minmax_element(values.begin(), values.end());
            const double minValue = *minIt;
            const double maxValue = *maxIt;
            const double range = maxValue - minValue;
            if (range > 1e-12) {
                for (auto& [symbol, value] : result.values) {
                    Q_UNUSED(symbol);
                    value = (value - minValue) / range;
                }
            }
        }
    };

    if (context.historicalView) {
        for (const QString& metric : selectedMetrics) {
            const double weight = metricWeight(metric);
            if (weight <= 0.0) {
                continue;
            }

            MetricContribution contribution;
            contribution.metric = metric;
            contribution.weight = weight;

            if (metric == QStringLiteral("cf_p")) {
                if (!context.historicalView->hasField("market_cap") || !context.historicalView->hasField("operating_cash_flow")) {
                    const QString errorMessage = QString::fromUtf8("缓存数据集缺少 market_cap 或 operating_cash_flow 字段，无法计算价值因子CF/P");
                    result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                    return result;
                }

                const auto marketCaps = context.historicalView->getCrossSection(effectiveDate.toStdString(), "market_cap", context.symbols);
                const auto cashFlows = context.historicalView->getCrossSection(effectiveDate.toStdString(), "operating_cash_flow", context.symbols);
                for (const auto& [symbol, marketCap] : marketCaps) {
                    const auto cashFlowIt = cashFlows.find(symbol);
                    if (cashFlowIt == cashFlows.end()) {
                        continue;
                    }
                    ++contribution.rawSampleCount;
                    const double operatingCashFlow = cashFlowIt->second;
                    if (marketCap <= 0.0 || operatingCashFlow <= 0.0) {
                        ++contribution.invalidSampleCount;
                        continue;
                    }
                    contribution.scores[symbol] = scoreFromMetricRawValue(metric, operatingCashFlow / marketCap);
                }
            } else {
                const QString field = selectedFieldForMetric(metric);
                if (field.isEmpty() || !context.historicalView->hasField(field.toStdString())) {
                    const QString errorMessage = QString("缓存数据集缺少字段 %1，无法计算价值因子").arg(field.isEmpty() ? metric : field);
                    result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                    return result;
                }

                const auto crossSection = context.historicalView->getCrossSection(effectiveDate.toStdString(), field.toStdString(), context.symbols);
                for (const auto& [symbol, rawValue] : crossSection) {
                    ++contribution.rawSampleCount;
                    if (rawValue <= 0.0) {
                        ++contribution.invalidSampleCount;
                        continue;
                    }
                    contribution.scores[symbol] = scoreFromMetricRawValue(metric, rawValue);
                }
            }

            metricContributions.push_back(std::move(contribution));
        }
    }

    if (metricContributions.empty()) {
        result.dataStatus = CalculationResult::createError("价值因子没有可用的指标权重配置").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("价值因子没有可用的指标权重配置"));
        return result;
    }

    std::unordered_map<std::string, double> weightedScores;
    std::unordered_map<std::string, double> usedWeights;
    int totalRawSampleCount = 0;
    int totalInvalidSampleCount = 0;
    for (const auto& contribution : metricContributions) {
        totalRawSampleCount += contribution.rawSampleCount;
        totalInvalidSampleCount += contribution.invalidSampleCount;
        for (const auto& [symbol, score] : contribution.scores) {
            weightedScores[symbol] += score * contribution.weight;
            usedWeights[symbol] += contribution.weight;
        }
    }

    for (const auto& [symbol, weightedScore] : weightedScores) {
        const auto weightIt = usedWeights.find(symbol);
        if (weightIt == usedWeights.end() || weightIt->second <= 0.0) {
            continue;
        }
        result.values[symbol] = weightedScore / weightIt->second;
    }

    if (result.values.empty()) {
        const QString emptyReason = totalRawSampleCount == 0
            ? QString::fromUtf8("当前价值因子没有可用的指标样本")
            : QString::fromUtf8("当前价值因子的多指标样本全部无效或非正数");
        result.metadata.set("emptyReason", json_helper::toJsonValue(emptyReason.toStdString()));
        result.metadata.set("rawSampleCount", json_helper::toJsonValue(totalRawSampleCount));
        result.metadata.set("nonPositiveSampleCount", json_helper::toJsonValue(totalInvalidSampleCount));
    }

    applyCrossSectionPostProcessing();

    auto valuationMetricsJson = foundation::json::JsonFacade::createArray();
    auto valuationWeightsJson = foundation::json::JsonFacade::createArray();
    for (const QString& metric : selectedMetrics) {
        valuationMetricsJson.push_back(json_helper::toJsonValue(metric.toStdString()));
        valuationWeightsJson.push_back(json_helper::toJsonValue(metricWeight(metric)));
    }
    result.metadata.set("valuationMetrics", valuationMetricsJson);
    result.metadata.set("valuationWeights", valuationWeightsJson);
    result.metadata.set("valuationMetric", json_helper::toJsonValue(selectedMetrics.front().toStdString()));
    result.metadata.set("effectiveDate", json_helper::toJsonValue(effectiveDate.toStdString()));
    result.metadata.set("frequency", json_helper::toJsonValue(frequency.toStdString()));
    result.metadata.set("laggedEnabled", json_helper::toJsonValue(params_.laggedEnabled));
    result.metadata.set("lookbackPeriod", json_helper::toJsonValue(params_.lookbackPeriod));
    result.metadata.set("usePercentile", json_helper::toJsonValue(params_.usePercentile));
    result.metadata.set("industryNeutral", json_helper::toJsonValue(params_.industryNeutral));
    result.metadata.set("standardization", json_helper::toJsonValue(standardization.toStdString()));
    result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
    return result;
}

DataRequirements ValueFactor::getDataRequirements() const {
    DataRequirements req;
    const auto appendUnique = [&req](const std::string& field) {
        if (std::find(req.requiredFields.begin(), req.requiredFields.end(), field) == req.requiredFields.end()) {
            req.requiredFields.push_back(field);
        }
    };

    std::vector<std::string> metrics = params_.valuationMetrics.empty()
        ? std::vector<std::string>{params_.valuationType}
        : params_.valuationMetrics;

    for (const auto& rawMetric : metrics) {
        const QString metric = normalizedMetric(rawMetric);
        if (metric == "bp") {
            appendUnique("pb_ratio");
        } else if (metric == "ep") {
            appendUnique("pe_ratio");
        } else if (metric == "dividend_yield") {
            appendUnique("dividend_yield");
        } else if (metric == "cf_p") {
            appendUnique("market_cap");
            appendUnique("operating_cash_flow");
        }
    }
    return req;
}

BoundaryRules ValueFactor::getBoundaryRules() const {
    BoundaryRules rules;
    rules.minDataPoints = 1;
    rules.handleOutliers = "winsorize_3sigma";
    return rules;
}

std::shared_ptr<ValueFactor> ValueFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker) {

    auto factor = std::make_shared<ValueFactor>();
    factor->dataChecker_ = dataChecker;
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

QString ValueFactor::selectedColumn() const {
    const QString metric = normalizedMetric(params_.valuationType);
    if (metric == "ep") {
        return "pe_ratio";
    }
    if (metric == "bp") {
        return "pb_ratio";
    }
    if (metric == "dividend_yield") {
        return "dividend_yield";
    }
    return {};
}

double ValueFactor::scoreFromRawValue(double rawValue) const {
    const QString metric = normalizedMetric(params_.valuationType);
    if (metric == "bp" || metric == "ep") {
        return 1.0 / rawValue;
    }
    if (metric == "dividend_yield") {
        return rawValue;
    }
    if (metric == "cf_p") {
        return rawValue;
    }
    return 0.0;
}

void ValueFactor::loadConfig(const foundation::json::JsonFacade& config) {
    BaseFactor::loadConfig(config);
    if (config.has("calculation")) {
        params_.fromJson(config.get("calculation"));
    }
    dataRequirements_.requiredFields = getDataRequirements().requiredFields;
}

} // namespace factor