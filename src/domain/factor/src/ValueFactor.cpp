#include "domain/factor/include/ValueFactor.h"
#include "domain/factor/include/FactorDataProvider.h"
#include "infrastructure/include/database/QtMySQLDatabase.h"

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

void ValueFactor::initializeFromDatabase(const std::string& instanceId) {
    BaseFactor::initializeFromDatabase(instanceId);
}

CalculationResult ValueFactor::calculate(const CalculationContext& context) {
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    if (context.dataProvider) {
        result.dataStatus.availability = DataAvailability::AVAILABLE;
        result.dataStatus.coverage = 1.0;
        result.dataStatus.message = "使用缓存数据集";
    } else {
        result.dataStatus = checkDataAvailability(context.date);
    }

    if (!result.dataStatus.isValid()) {
        result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
        return result;
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

        if (context.dataProvider) {
            const std::vector<std::string> symbols = context.symbols.empty()
                ? context.dataProvider->getAvailableSymbols(context.date)
                : context.symbols;
            for (int offset = 1; offset <= 45; ++offset) {
                const QString candidate = QDate::fromString(anchorDate, Qt::ISODate).addDays(-offset).toString(Qt::ISODate);
                if (context.dataProvider->getCrossSection(candidate.toStdString(), requiredField.toStdString(), symbols).empty()) {
                    continue;
                }
                return candidate;
            }
        }

        if (db_) {
            const QString sql = QString(
                "SELECT MAX(trade_date) AS trade_date FROM daily_bar "
                "WHERE trade_date < :date AND %1 IS NOT NULL"
            ).arg(requiredField);
            auto queryResult = db_->executeQuery(sql, {{":date", anchorDate}});
            if (!queryResult.isEmpty()) {
                const QString resolvedDate = queryResult.getRow(0).getString("trade_date");
                if (!resolvedDate.isEmpty()) {
                    return resolvedDate;
                }
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

        if (params_.industryNeutral && db_) {
            QString industrySql = QStringLiteral("SELECT symbol, industry FROM symbol_info");
            auto industryResult = db_->executeQuery(industrySql, {});
            std::unordered_map<std::string, QString> industryBySymbol;
            for (size_t i = 0; i < industryResult.rowCount(); ++i) {
                const auto& row = industryResult.getRow(i);
                industryBySymbol[row.getString("symbol").toStdString()] = row.getString("industry");
            }

            std::unordered_map<QString, std::vector<double>> groupedValues;
            for (const auto& [symbol, value] : result.values) {
                const auto industryIt = industryBySymbol.find(symbol);
                if (industryIt != industryBySymbol.end() && !industryIt->second.isEmpty() && std::isfinite(value)) {
                    groupedValues[industryIt->second].push_back(value);
                }
            }

            std::unordered_map<QString, double> industryMean;
            for (const auto& [industry, values] : groupedValues) {
                industryMean[industry] = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
            }

            for (auto& [symbol, value] : result.values) {
                const auto industryIt = industryBySymbol.find(symbol);
                if (industryIt != industryBySymbol.end()) {
                    const auto meanIt = industryMean.find(industryIt->second);
                    if (meanIt != industryMean.end()) {
                        value -= meanIt->second;
                    }
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

    if (context.dataProvider) {
        for (const QString& metric : selectedMetrics) {
            const double weight = metricWeight(metric);
            if (weight <= 0.0) {
                continue;
            }

            MetricContribution contribution;
            contribution.metric = metric;
            contribution.weight = weight;

            if (metric == QStringLiteral("cf_p")) {
                if (!context.dataProvider->hasField("market_cap") || !context.dataProvider->hasField("operating_cash_flow")) {
                    const QString errorMessage = QString::fromUtf8("缓存数据集缺少 market_cap 或 operating_cash_flow 字段，无法计算价值因子CF/P");
                    result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                    return result;
                }

                const auto marketCaps = context.dataProvider->getCrossSection(effectiveDate.toStdString(), "market_cap", context.symbols);
                const auto cashFlows = context.dataProvider->getCrossSection(effectiveDate.toStdString(), "operating_cash_flow", context.symbols);
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
                if (field.isEmpty() || !context.dataProvider->hasField(field.toStdString())) {
                    const QString errorMessage = QString("缓存数据集缺少字段 %1，无法计算价值因子").arg(field.isEmpty() ? metric : field);
                    result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                    return result;
                }

                const auto crossSection = context.dataProvider->getCrossSection(effectiveDate.toStdString(), field.toStdString(), context.symbols);
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
    } else if (!db_) {
        result.dataStatus = CalculationResult::createError("数据库连接未初始化").dataStatus;
        return result;
    } else {
        const std::unordered_set<std::string> requestedSymbols(context.symbols.begin(), context.symbols.end());
        for (const QString& metric : selectedMetrics) {
            const double weight = metricWeight(metric);
            if (weight <= 0.0) {
                continue;
            }

            MetricContribution contribution;
            contribution.metric = metric;
            contribution.weight = weight;

            if (metric == QStringLiteral("cf_p")) {
                const QString sql = QString(
                "SELECT db.symbol, db.market_cap, fi.operating_cash_flow "
                    "FROM daily_bar db "
                    "JOIN symbol_info si ON si.symbol = db.symbol "
                    "JOIN ("
                    "    SELECT base.symbol_id, MAX(base.trade_date) AS latest_trade_date "
                    "    FROM financial_indicator_daily base "
                    "    WHERE base.trade_date <= :date AND base.report_date >= :min_report_date "
                    "    GROUP BY base.symbol_id"
                    ") latest ON latest.symbol_id = si.symbol_id "
                    "JOIN financial_indicator_daily fi ON fi.symbol_id = latest.symbol_id AND fi.trade_date = latest.latest_trade_date "
                    "WHERE db.trade_date = :date "
                    "  AND db.market_cap IS NOT NULL AND db.market_cap > 0 "
                "  AND fi.operating_cash_flow IS NOT NULL AND fi.operating_cash_flow > 0 "
                    "ORDER BY db.symbol");

                const QString minReportDate = QDate::fromString(effectiveDate, Qt::ISODate)
                    .addDays(-(std::max)(1, params_.lookbackPeriod)).toString(Qt::ISODate);
                auto queryResult = db_->executeQuery(sql, {{":date", effectiveDate}, {":min_report_date", minReportDate}});
                for (size_t i = 0; i < queryResult.rowCount(); ++i) {
                    const auto& row = queryResult.getRow(i);
                    const std::string symbol = row.getString("symbol").toStdString();
                    if (!requestedSymbols.empty() && requestedSymbols.find(symbol) == requestedSymbols.end()) {
                        continue;
                    }
                    ++contribution.rawSampleCount;
                    const double marketCap = row.getDouble("market_cap");
                    const double operatingCashFlow = row.getDouble("operating_cash_flow");
                    if (marketCap <= 0.0 || operatingCashFlow <= 0.0) {
                        ++contribution.invalidSampleCount;
                        continue;
                    }
                    contribution.scores[symbol] = scoreFromMetricRawValue(metric, operatingCashFlow / marketCap);
                }
            } else {
                const QString field = selectedFieldForMetric(metric);
                if (field.isEmpty()) {
                    const QString errorMessage = QString("当前运行时暂不支持计算价值因子指标 %1").arg(metric);
                    result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                    return result;
                }

                QString sql = QString("SELECT symbol, %1 AS factor_raw FROM daily_bar WHERE trade_date = :date AND %1 IS NOT NULL")
                    .arg(field);

                auto queryResult = db_->executeQuery(sql, {{":date", effectiveDate}});
                for (size_t i = 0; i < queryResult.rowCount(); ++i) {
                    const auto& row = queryResult.getRow(i);
                    const std::string symbol = row.getString("symbol").toStdString();
                    if (!requestedSymbols.empty() && requestedSymbols.find(symbol) == requestedSymbols.end()) {
                        continue;
                    }
                    ++contribution.rawSampleCount;
                    const double rawValue = row.getDouble("factor_raw");
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
        result.metadata.set("empty_reason", json_helper::toJsonValue(emptyReason.toStdString()));
        result.metadata.set("raw_sample_count", json_helper::toJsonValue(totalRawSampleCount));
        result.metadata.set("non_positive_sample_count", json_helper::toJsonValue(totalInvalidSampleCount));
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
    result.metadata.set("effective_date", json_helper::toJsonValue(effectiveDate.toStdString()));
    result.metadata.set("frequency", json_helper::toJsonValue(frequency.toStdString()));
    result.metadata.set("lagged_enabled", json_helper::toJsonValue(params_.laggedEnabled));
    result.metadata.set("lookback_period", json_helper::toJsonValue(params_.lookbackPeriod));
    result.metadata.set("use_percentile", json_helper::toJsonValue(params_.usePercentile));
    result.metadata.set("industry_neutral", json_helper::toJsonValue(params_.industryNeutral));
    result.metadata.set("standardization", json_helper::toJsonValue(standardization.toStdString()));
    result.metadata.set("symbol_count", json_helper::toJsonValue(static_cast<int>(result.values.size())));
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
    const std::string& instanceId,
    std::shared_ptr<astock::database::QtMySQLDatabase> db,
    std::shared_ptr<DataAvailabilityChecker> dataChecker) {

    auto factor = std::make_shared<ValueFactor>();
    factor->db_ = db;
    factor->dataChecker_ = dataChecker;
    factor->initializeFromDatabase(instanceId);
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