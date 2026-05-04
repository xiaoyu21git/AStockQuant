#include "domain/factor/include/SizeFactor.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include "domain/factor/include/HistoricalView.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_set>

namespace factor {

namespace {

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

SizeFactor::SizeFactor() {
    factorType_ = "规模因子";
}

CalculationResult SizeFactor::calculate(const CalculationContext& context) {
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(
            context,
            QStringLiteral("已移除规模因子运行期数据库取数路径，请由引擎提供 HistoricalView").toStdString());
    }

    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用缓存数据集";

    if (isHistoricalViewRuntime(context) && params_.industryNeutral) {
        return createHistoricalViewRuntimeError(
            context,
            QStringLiteral("规模因子 HistoricalView 回测已禁止行业中性化数据库回退，请由引擎提供中性化后的输入或关闭 industryNeutral")
                .toStdString());
    }

    const QString column = selectedColumn();
    if (column.isEmpty()) {
        const QString metric = QString::fromStdString(params_.sizeMetric).trimmed().toLower();
        const QString errorMessage = QString("当前运行时暂不支持计算规模因子指标 %1").arg(metric.isEmpty() ? QString("unknown") : metric);
        result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
        result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
        return result;
    }

    if (context.historicalView && !context.historicalView->hasField(column.toStdString())) {
        const QString errorMessage = QString("缓存数据集缺少字段 %1，无法计算规模因子").arg(column);
        result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
        result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
        return result;
    }

    if (context.historicalView && context.historicalView->hasField(column.toStdString())) {
        const auto crossSection = context.historicalView->getCrossSection(context.date, column.toStdString(), context.symbols);
        for (const auto& [symbol, rawValue] : crossSection) {
            if (rawValue <= 0.0) {
                continue;
            }
            result.values[symbol] = scoreFromRawValue(rawValue);
        }
    }

    const QString standardization = normalizedStandardization(params_.standardization);
    if (!result.values.empty()) {
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
        } else {
            finiteValues.clear();
            finiteValues.reserve(result.values.size());
            for (const auto& [symbol, value] : result.values) {
                Q_UNUSED(symbol);
                if (std::isfinite(value)) {
                    finiteValues.push_back(value);
                }
            }

            if (!finiteValues.empty() && standardization == QStringLiteral("zscore")) {
                const double mean = std::accumulate(finiteValues.begin(), finiteValues.end(), 0.0) / static_cast<double>(finiteValues.size());
                double variance = 0.0;
                for (double value : finiteValues) {
                    const double delta = value - mean;
                    variance += delta * delta;
                }
                const double stdev = std::sqrt(variance / static_cast<double>(finiteValues.size()));
                if (stdev > 1e-12) {
                    for (auto& [symbol, value] : result.values) {
                        Q_UNUSED(symbol);
                        value = (value - mean) / stdev;
                    }
                }
            } else if (!finiteValues.empty() && standardization == QStringLiteral("minmax")) {
                const auto [minIt, maxIt] = std::minmax_element(finiteValues.begin(), finiteValues.end());
                const double range = *maxIt - *minIt;
                if (range > 1e-12) {
                    for (auto& [symbol, value] : result.values) {
                        Q_UNUSED(symbol);
                        value = (value - *minIt) / range;
                    }
                }
            }
        }
    }

    result.metadata.set("sizeMetric", json_helper::toJsonValue(params_.sizeMetric));
    result.metadata.set("logTransform", json_helper::toJsonValue(params_.logTransform));
    result.metadata.set("usePercentile", json_helper::toJsonValue(params_.usePercentile));
    result.metadata.set("industryNeutral", json_helper::toJsonValue(params_.industryNeutral));
    result.metadata.set("standardization", json_helper::toJsonValue(standardization.toStdString()));
    result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
    return result;
}

DataRequirements SizeFactor::getDataRequirements() const {
    DataRequirements req;
    req.requiredFields = {selectedColumn().toStdString()};
    return req;
}

BoundaryRules SizeFactor::getBoundaryRules() const {
    BoundaryRules rules;
    rules.minDataPoints = 1;
    rules.handleOutliers = "winsorize_3sigma";
    return rules;
}

std::shared_ptr<SizeFactor> SizeFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker) {

    auto factor = std::make_shared<SizeFactor>();
    factor->dataChecker_ = dataChecker;
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

QString SizeFactor::selectedColumn() const {
    const QString metric = QString::fromStdString(params_.sizeMetric).trimmed().toLower();
    if (metric == "market_cap" || metric == QString::fromUtf8("总市值")) {
        return "market_cap";
    }
    if (metric == "circulating_market_cap") {
        return "circulating_market_cap";
    }
    if (metric == QString::fromUtf8("流通市值")) {
        return "circulating_market_cap";
    }
    if (metric == "total_assets" || metric == QString::fromUtf8("总资产")) {
        return "total_assets";
    }
    return {};
}

double SizeFactor::scoreFromRawValue(double rawValue) const {
    if (params_.logTransform) {
        return -std::log(rawValue);
    }
    return -rawValue;
}

void SizeFactor::loadConfig(const foundation::json::JsonFacade& config) {
    BaseFactor::loadConfig(config);
    if (config.has("calculation")) {
        params_.fromJson(config.get("calculation"));
    }
    dataRequirements_.requiredFields = {selectedColumn().toStdString()};
}

} // namespace factor