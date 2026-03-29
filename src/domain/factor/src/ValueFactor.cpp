#include "domain/factor/include/ValueFactor.h"
#include "domain/factor/include/FactorDataProvider.h"
#include "infrastructure/include/database/QtMySQLDatabase.h"

#include <algorithm>
#include <cmath>

namespace factor {

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

    const QString column = selectedColumn();
    if (column.isEmpty()) {
        const QString metric = QString::fromStdString(params_.valuationType).trimmed().toLower();
        const QString errorMessage = QString("当前运行时暂不支持计算价值因子指标 %1").arg(metric.isEmpty() ? QString("unknown") : metric);
        result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
        result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
        return result;
    }

    if (context.dataProvider && !context.dataProvider->hasField(column.toStdString())) {
        const QString errorMessage = QString("缓存数据集缺少字段 %1，无法计算价值因子").arg(column);
        result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
        result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
        return result;
    }

    if (context.dataProvider && context.dataProvider->hasField(column.toStdString())) {
        const auto crossSection = context.dataProvider->getCrossSection(context.date, column.toStdString(), context.symbols);
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
                    .arg(QString::fromStdString(context.date), column);
            } else if (nonPositiveSampleCount == static_cast<int>(crossSection.size())) {
                emptyReason = QString("当前缓存数据集在 %1 的 %2 全部为 0 或非正数")
                    .arg(QString::fromStdString(context.date), column);
            } else {
                emptyReason = QString("当前缓存数据集在 %1 没有有效的 %2 > 0 样本")
                    .arg(QString::fromStdString(context.date), column);
            }
            result.metadata.set("empty_reason", json_helper::toJsonValue(emptyReason.toStdString()));
            result.metadata.set("raw_sample_count", json_helper::toJsonValue(static_cast<int>(crossSection.size())));
            result.metadata.set("non_positive_sample_count", json_helper::toJsonValue(nonPositiveSampleCount));
        }
    } else if (!db_) {
        result.dataStatus = CalculationResult::createError("数据库连接未初始化").dataStatus;
        return result;
    } else {
        QString sql = QString("SELECT symbol, %1 AS factor_raw FROM daily_bar WHERE trade_date = :date AND %1 IS NOT NULL")
            .arg(column);

        auto queryResult = db_->executeQuery(sql, {{":date", QString::fromStdString(context.date)}});
        int nonPositiveSampleCount = 0;
        for (size_t i = 0; i < queryResult.rowCount(); ++i) {
            const auto& row = queryResult.getRow(i);
            const double rawValue = row.getDouble("factor_raw");
            if (rawValue <= 0.0) {
                ++nonPositiveSampleCount;
                continue;
            }
            result.values[row.getString("symbol").toStdString()] = scoreFromRawValue(rawValue);
        }

        if (result.values.empty()) {
            QString emptyReason;
            if (queryResult.rowCount() == 0) {
                emptyReason = QString("daily_bar 在 %1 没有可用的 %2 字段值")
                    .arg(QString::fromStdString(context.date), column);
            } else if (nonPositiveSampleCount == static_cast<int>(queryResult.rowCount())) {
                emptyReason = QString("daily_bar 在 %1 的 %2 全部为 0 或非正数")
                    .arg(QString::fromStdString(context.date), column);
            } else {
                emptyReason = QString("daily_bar 在 %1 没有有效的 %2 > 0 样本")
                    .arg(QString::fromStdString(context.date), column);
            }
            result.metadata.set("empty_reason", json_helper::toJsonValue(emptyReason.toStdString()));
            result.metadata.set("raw_sample_count", json_helper::toJsonValue(static_cast<int>(queryResult.rowCount())));
            result.metadata.set("non_positive_sample_count", json_helper::toJsonValue(nonPositiveSampleCount));
        }
    }

    result.metadata.set("valuation_type", json_helper::toJsonValue(params_.valuationType));
    result.metadata.set("symbol_count", json_helper::toJsonValue(static_cast<int>(result.values.size())));
    return result;
}

DataRequirements ValueFactor::getDataRequirements() const {
    return dataRequirements_;
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
    const QString metric = QString::fromStdString(params_.valuationType).trimmed().toLower();
    if (metric == "pe" || metric == "pe_ttm") {
        return "pe_ratio";
    }
    if (metric == "pb") {
        return "pb_ratio";
    }
    if (metric == "market_cap") {
        return "market_cap";
    }
    return {};
}

double ValueFactor::scoreFromRawValue(double rawValue) const {
    const QString metric = QString::fromStdString(params_.valuationType).trimmed().toLower();
    if (metric == "market_cap") {
        return rawValue > 0.0 ? 1.0 / std::log(rawValue + 1.0) : 0.0;
    }
    return rawValue > 0.0 ? 1.0 / rawValue : 0.0;
}

void ValueFactor::loadConfig(const foundation::json::JsonFacade& config) {
    BaseFactor::loadConfig(config);
    if (config.has("calculation")) {
        params_.fromJson(config.get("calculation"));
    }
}

} // namespace factor