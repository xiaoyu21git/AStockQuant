#include "domain/factor/include/SizeFactor.h"
#include "domain/factor/include/FactorDataProvider.h"
#include "infrastructure/include/database/QtMySQLDatabase.h"

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

void SizeFactor::initializeFromDatabase(const std::string& instanceId) {
    BaseFactor::initializeFromDatabase(instanceId);
}

CalculationResult SizeFactor::calculate(const CalculationContext& context) {
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

    const QString column = selectedColumn();
    if (column.isEmpty()) {
        const QString metric = QString::fromStdString(params_.sizeMetric).trimmed().toLower();
        const QString errorMessage = QString("当前运行时暂不支持计算规模因子指标 %1").arg(metric.isEmpty() ? QString("unknown") : metric);
        result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
        result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
        return result;
    }

    if (context.dataProvider && !context.dataProvider->hasField(column.toStdString())) {
        const QString errorMessage = QString("缓存数据集缺少字段 %1，无法计算规模因子").arg(column);
        result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
        result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
        return result;
    }

    if (context.dataProvider && context.dataProvider->hasField(column.toStdString())) {
        const auto crossSection = context.dataProvider->getCrossSection(context.date, column.toStdString(), context.symbols);
        for (const auto& [symbol, rawValue] : crossSection) {
            if (rawValue <= 0.0) {
                continue;
            }
            result.values[symbol] = scoreFromRawValue(rawValue);
        }
    } else if (!db_) {
        result.dataStatus = CalculationResult::createError("数据库连接未初始化").dataStatus;
        return result;
    } else {
        const std::unordered_set<std::string> requestedSymbols(context.symbols.begin(), context.symbols.end());
        astock::database::QueryResult queryResult;
        if (column == QStringLiteral("total_assets")) {
            queryResult = db_->executeQuery(
                QString(
                    "SELECT si.symbol, fi.total_assets AS factor_raw "
                    "FROM financial_indicator_daily fi "
                    "JOIN symbol_info si ON si.symbol_id = fi.symbol_id "
                    "JOIN ("
                    "    SELECT base.symbol_id, MAX(base.trade_date) AS latest_trade_date "
                    "    FROM financial_indicator_daily base "
                    "    WHERE base.trade_date <= :date "
                    "    GROUP BY base.symbol_id"
                    ") latest ON latest.symbol_id = fi.symbol_id AND latest.latest_trade_date = fi.trade_date "
                    "WHERE fi.total_assets IS NOT NULL AND fi.total_assets > 0 "
                    "ORDER BY si.symbol"),
                {{":date", QString::fromStdString(context.date)}}
            );
        } else {
            queryResult = db_->executeQuery(
                QString("SELECT symbol, %1 AS factor_raw FROM daily_bar WHERE trade_date = :date AND %1 IS NOT NULL AND %1 > 0")
                    .arg(column),
                {{":date", QString::fromStdString(context.date)}}
            );
        }

        for (size_t i = 0; i < queryResult.rowCount(); ++i) {
            const auto& row = queryResult.getRow(i);
            const std::string symbol = row.getString("symbol").toStdString();
            if (!requestedSymbols.empty() && requestedSymbols.find(symbol) == requestedSymbols.end()) {
                continue;
            }
            const double rawValue = row.getDouble("factor_raw");
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

        if (params_.industryNeutral && db_) {
            auto industryResult = db_->executeQuery(QStringLiteral("SELECT symbol, industry FROM symbol_info"), {});
            std::unordered_map<std::string, QString> industryBySymbol;
            industryBySymbol.reserve(industryResult.rowCount());
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
                if (industryIt == industryBySymbol.end()) {
                    continue;
                }
                const auto meanIt = industryMean.find(industryIt->second);
                if (meanIt != industryMean.end()) {
                    value -= meanIt->second;
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

    result.metadata.set("size_metric", json_helper::toJsonValue(params_.sizeMetric));
    result.metadata.set("log_transform", json_helper::toJsonValue(params_.logTransform));
    result.metadata.set("use_percentile", json_helper::toJsonValue(params_.usePercentile));
    result.metadata.set("industry_neutral", json_helper::toJsonValue(params_.industryNeutral));
    result.metadata.set("standardization", json_helper::toJsonValue(standardization.toStdString()));
    result.metadata.set("symbol_count", json_helper::toJsonValue(static_cast<int>(result.values.size())));
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
    const std::string& instanceId,
    std::shared_ptr<astock::database::QtMySQLDatabase> db,
    std::shared_ptr<DataAvailabilityChecker> dataChecker) {

    auto factor = std::make_shared<SizeFactor>();
    factor->db_ = db;
    factor->dataChecker_ = dataChecker;
    factor->initializeFromDatabase(instanceId);
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