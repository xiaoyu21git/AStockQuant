#include "domain/factor/include/SizeFactor.h"
#include "domain/factor/include/FactorDataProvider.h"
#include "infrastructure/include/database/QtMySQLDatabase.h"

#include <cmath>

namespace factor {

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
        auto queryResult = db_->executeQuery(
            QString("SELECT symbol, %1 AS factor_raw FROM daily_bar WHERE trade_date = :date AND %1 IS NOT NULL AND %1 > 0")
                .arg(column),
            {{":date", QString::fromStdString(context.date)}}
        );

        for (size_t i = 0; i < queryResult.rowCount(); ++i) {
            const auto& row = queryResult.getRow(i);
            const double rawValue = row.getDouble("factor_raw");
            if (rawValue <= 0.0) {
                continue;
            }
            result.values[row.getString("symbol").toStdString()] = scoreFromRawValue(rawValue);
        }
    }

    result.metadata.set("size_metric", json_helper::toJsonValue(params_.sizeMetric));
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
    if (metric == "circulating_market_cap") {
        return "circulating_market_cap";
    }
    return "market_cap";
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