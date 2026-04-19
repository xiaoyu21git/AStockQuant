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
    return QString::fromStdString(metric).trimmed().toLower();
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

    const QString metric = normalizedMetric(params_.valuationType);
    const QString column = selectedColumn();
    if (metric != "ps" && column.isEmpty()) {
        const QString errorMessage = QString("当前运行时暂不支持计算价值因子指标 %1").arg(metric.isEmpty() ? QString("unknown") : metric);
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
        effectiveDate = resolvePreviousAvailableDate(effectiveDate, metric == "ps" ? QStringLiteral("market_cap") : column);
    }

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
        if (metric == "ps") {
            if (!context.dataProvider->hasField("market_cap") || !context.dataProvider->hasField("total_revenue")) {
                const QString errorMessage = QString::fromUtf8("缓存数据集缺少 market_cap 或 total_revenue 字段，无法计算价值因子市销率");
                result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                return result;
            }

            const auto marketCaps = context.dataProvider->getCrossSection(effectiveDate.toStdString(), "market_cap", context.symbols);
            const auto revenues = context.dataProvider->getCrossSection(effectiveDate.toStdString(), "total_revenue", context.symbols);
            int rawSampleCount = 0;
            int invalidSampleCount = 0;
            for (const auto& [symbol, marketCap] : marketCaps) {
                const auto revenueIt = revenues.find(symbol);
                if (revenueIt == revenues.end()) {
                    continue;
                }
                ++rawSampleCount;
                const double totalRevenue = revenueIt->second;
                if (marketCap <= 0.0 || totalRevenue <= 0.0) {
                    ++invalidSampleCount;
                    continue;
                }
                result.values[symbol] = scoreFromRawValue(marketCap / totalRevenue);
            }

            if (result.values.empty()) {
                const QString emptyReason = rawSampleCount == 0
                    ? QString::fromUtf8("当前缓存数据集缺少可用于计算市销率的 market_cap/total_revenue 样本")
                    : QString::fromUtf8("当前缓存数据集的市销率样本全部无效或非正数");
                result.metadata.set("empty_reason", json_helper::toJsonValue(emptyReason.toStdString()));
                result.metadata.set("raw_sample_count", json_helper::toJsonValue(rawSampleCount));
                result.metadata.set("non_positive_sample_count", json_helper::toJsonValue(invalidSampleCount));
            }
        } else {
            if (!context.dataProvider->hasField(column.toStdString())) {
                const QString errorMessage = QString("缓存数据集缺少字段 %1，无法计算价值因子").arg(column);
                result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                return result;
            }

            const auto crossSection = context.dataProvider->getCrossSection(effectiveDate.toStdString(), column.toStdString(), context.symbols);
            int nonPositiveSampleCount = 0;
            for (const auto& [symbol, rawValue] : crossSection) {
                if (rawValue <= 0.0) {
                    ++nonPositiveSampleCount;
                    continue;
                }
                result.values[symbol] = scoreFromRawValue(rawValue);
            }

            if (result.values.empty()) {
                QString emptyReason;
                if (crossSection.empty()) {
                    emptyReason = QString("当前缓存数据集在 %1 没有可用的 %2 字段值")
                        .arg(effectiveDate, column);
                } else if (nonPositiveSampleCount == static_cast<int>(crossSection.size())) {
                    emptyReason = QString("当前缓存数据集在 %1 的 %2 全部为 0 或非正数")
                        .arg(effectiveDate, column);
                } else {
                    emptyReason = QString("当前缓存数据集在 %1 没有有效的 %2 > 0 样本")
                        .arg(effectiveDate, column);
                }
                result.metadata.set("empty_reason", json_helper::toJsonValue(emptyReason.toStdString()));
                result.metadata.set("raw_sample_count", json_helper::toJsonValue(static_cast<int>(crossSection.size())));
                result.metadata.set("non_positive_sample_count", json_helper::toJsonValue(nonPositiveSampleCount));
            }
        }
    } else if (!db_) {
        result.dataStatus = CalculationResult::createError("数据库连接未初始化").dataStatus;
        return result;
    } else {
        const std::unordered_set<std::string> requestedSymbols(context.symbols.begin(), context.symbols.end());
        if (metric == "ps") {
            const QString sql = QString(
                "SELECT db.symbol, db.market_cap, fi.total_revenue "
                "FROM daily_bar db "
                "JOIN symbol_info si ON si.symbol = db.symbol "
                "JOIN ("
                "    SELECT base.symbol_id, MAX(base.report_date) AS latest_report_date "
                "    FROM financial_indicator base "
                "    WHERE base.report_date <= :date AND base.report_date >= :min_report_date "
                "    GROUP BY base.symbol_id"
                ") latest ON latest.symbol_id = si.symbol_id "
                "JOIN financial_indicator fi ON fi.symbol_id = latest.symbol_id AND fi.report_date = latest.latest_report_date "
                "WHERE db.trade_date = :date "
                "  AND db.market_cap IS NOT NULL AND db.market_cap > 0 "
                "  AND fi.total_revenue IS NOT NULL AND fi.total_revenue > 0 "
                "ORDER BY db.symbol");

            const QString minReportDate = QDate::fromString(effectiveDate, Qt::ISODate)
                .addDays(-(std::max)(1, params_.lookbackPeriod)).toString(Qt::ISODate);
            auto queryResult = db_->executeQuery(sql, {{":date", effectiveDate}, {":min_report_date", minReportDate}});
            int rawSampleCount = 0;
            int invalidSampleCount = 0;
            for (size_t i = 0; i < queryResult.rowCount(); ++i) {
                const auto& row = queryResult.getRow(i);
                const std::string symbol = row.getString("symbol").toStdString();
                if (!requestedSymbols.empty() && requestedSymbols.find(symbol) == requestedSymbols.end()) {
                    continue;
                }
                ++rawSampleCount;
                const double marketCap = row.getDouble("market_cap");
                const double totalRevenue = row.getDouble("total_revenue");
                if (marketCap <= 0.0 || totalRevenue <= 0.0) {
                    ++invalidSampleCount;
                    continue;
                }
                result.values[symbol] = scoreFromRawValue(marketCap / totalRevenue);
            }

            if (result.values.empty()) {
                const QString emptyReason = rawSampleCount == 0
                    ? QString::fromUtf8("未查询到可用于计算市销率的 market_cap/total_revenue 数据")
                    : QString::fromUtf8("市销率样本全部无效或非正数");
                result.metadata.set("empty_reason", json_helper::toJsonValue(emptyReason.toStdString()));
                result.metadata.set("raw_sample_count", json_helper::toJsonValue(rawSampleCount));
                result.metadata.set("non_positive_sample_count", json_helper::toJsonValue(invalidSampleCount));
            }
        } else {
            QString sql = QString("SELECT symbol, %1 AS factor_raw FROM daily_bar WHERE trade_date = :date AND %1 IS NOT NULL")
                .arg(column);

            auto queryResult = db_->executeQuery(sql, {{":date", effectiveDate}});
            int nonPositiveSampleCount = 0;
            for (size_t i = 0; i < queryResult.rowCount(); ++i) {
                const auto& row = queryResult.getRow(i);
                const std::string symbol = row.getString("symbol").toStdString();
                if (!requestedSymbols.empty() && requestedSymbols.find(symbol) == requestedSymbols.end()) {
                    continue;
                }
                const double rawValue = row.getDouble("factor_raw");
                if (rawValue <= 0.0) {
                    ++nonPositiveSampleCount;
                    continue;
                }
                result.values[symbol] = scoreFromRawValue(rawValue);
            }

            if (result.values.empty()) {
                QString emptyReason;
                if (queryResult.rowCount() == 0) {
                    emptyReason = QString("daily_bar 在 %1 没有可用的 %2 字段值")
                        .arg(effectiveDate, column);
                } else if (nonPositiveSampleCount == static_cast<int>(queryResult.rowCount())) {
                    emptyReason = QString("daily_bar 在 %1 的 %2 全部为 0 或非正数")
                        .arg(effectiveDate, column);
                } else {
                    emptyReason = QString("daily_bar 在 %1 没有有效的 %2 > 0 样本")
                        .arg(effectiveDate, column);
                }
                result.metadata.set("empty_reason", json_helper::toJsonValue(emptyReason.toStdString()));
                result.metadata.set("raw_sample_count", json_helper::toJsonValue(static_cast<int>(queryResult.rowCount())));
                result.metadata.set("non_positive_sample_count", json_helper::toJsonValue(nonPositiveSampleCount));
            }
        }
    }

    applyCrossSectionPostProcessing();

    result.metadata.set("valuation_type", json_helper::toJsonValue(params_.valuationType));
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
    const QString metric = normalizedMetric(params_.valuationType);
    if (metric == "pb") {
        req.requiredFields = {"pb_ratio"};
    } else if (metric == "market_cap") {
        req.requiredFields = {"market_cap"};
    } else if (metric == "dividend_yield") {
        req.requiredFields = {"dividend_yield"};
    } else if (metric == "ps") {
        req.requiredFields = {"market_cap", "total_revenue"};
    } else {
        req.requiredFields = {"pe_ratio"};
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
    if (metric == "pe" || metric == "pe_ttm" || metric.startsWith(QString::fromUtf8("市盈率"))) {
        return "pe_ratio";
    }
    if (metric == "pb" || metric == QString::fromUtf8("市净率")) {
        return "pb_ratio";
    }
    if (metric == "market_cap" || metric == QString::fromUtf8("总市值")) {
        return "market_cap";
    }
    if (metric == "dividend_yield" || metric == QString::fromUtf8("股息率")) {
        return "dividend_yield";
    }
    return {};
}

double ValueFactor::scoreFromRawValue(double rawValue) const {
    const QString metric = normalizedMetric(params_.valuationType);
    if (metric == "market_cap") {
        return rawValue > 0.0 ? 1.0 / std::log(rawValue + 1.0) : 0.0;
    }
    if (metric == "dividend_yield") {
        return rawValue > 0.0 ? rawValue : 0.0;
    }
    return rawValue > 0.0 ? 1.0 / rawValue : 0.0;
}

void ValueFactor::loadConfig(const foundation::json::JsonFacade& config) {
    BaseFactor::loadConfig(config);
    if (config.has("calculation")) {
        params_.fromJson(config.get("calculation"));
    }
    dataRequirements_.requiredFields = getDataRequirements().requiredFields;
}

} // namespace factor