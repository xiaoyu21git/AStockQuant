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
    if (context.dataProvider && !context.dataProvider->hasField(column.toStdString())) {
        const QString errorMessage = QString("缓存数据集缺少字段 %1，无法计算价值因子").arg(column);
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
        QString sql = QString("SELECT symbol, %1 AS factor_raw FROM daily_bar WHERE trade_date = :date AND %1 IS NOT NULL AND %1 > 0")
            .arg(column);

        auto queryResult = db_->executeQuery(sql, {{":date", QString::fromStdString(context.date)}});
        for (size_t i = 0; i < queryResult.rowCount(); ++i) {
            const auto& row = queryResult.getRow(i);
            const double rawValue = row.getDouble("factor_raw");
            if (rawValue <= 0.0) {
                continue;
            }
            result.values[row.getString("symbol").toStdString()] = scoreFromRawValue(rawValue);
        }
    }

    result.metadata.set("valuation_type", json_helper::toJsonValue(params_.valuationType));
    result.metadata.set("symbol_count", json_helper::toJsonValue(static_cast<int>(result.values.size())));
    return result;
}

DataRequirements ValueFactor::getDataRequirements() const {
    DataRequirements req;
    const QString column = selectedColumn();
    req.requiredFields = {column.toStdString()};
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
    const QString metric = QString::fromStdString(params_.valuationType).trimmed().toLower();
    if (metric == "pb") {
        return "pb_ratio";
    }
    if (metric == "market_cap") {
        return "market_cap";
    }
    return "pe_ratio";
}

double ValueFactor::scoreFromRawValue(double rawValue) const {
    if (params_.valuationType == "market_cap") {
        return rawValue > 0.0 ? 1.0 / std::log(rawValue + 1.0) : 0.0;
    }
    return rawValue > 0.0 ? 1.0 / rawValue : 0.0;
}

void ValueFactor::loadConfig(const foundation::json::JsonFacade& config) {
    BaseFactor::loadConfig(config);
    if (config.has("calculation")) {
        params_.fromJson(config.get("calculation"));
    }
    dataRequirements_.requiredFields = {selectedColumn().toStdString()};
}

} // namespace factor