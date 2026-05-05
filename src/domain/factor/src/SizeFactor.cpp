#include "domain/factor/include/SizeFactor.h"
#include "domain/factor/include/FactorInstanceManager.h"

#include <algorithm>
#include <cmath>

namespace factor {

namespace {

}

SizeFactor::SizeFactor() {
    factorType_ = "规模因子";
}

CalculationResult SizeFactor::calculate(const CalculationContext& context) {
    const QString column = selectedColumn();
    if (column.isEmpty()) {
        const QString metric = QString::fromStdString(params_.sizeMetric).trimmed().toLower();
        CalculationResult result = CalculationResult::createError(
            QString("当前运行时暂不支持计算规模因子指标 %1").arg(metric.isEmpty() ? QString("unknown") : metric).toStdString());
        result.date = context.date;
        result.calculationId = foundation::utils::Uuid::generate_v4();
        result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
        return result;
    }

    const CommonFactorParams commonParams{
        params_.lookbackPeriod,
        params_.laggedEnabled,
        params_.frequency,
        params_.standardization,
        params_.neutralizationEnabled};

    return executeWithCommonParams(
        context,
        commonParams,
        QStringList{column},
        [this, &context, column](const CommonFactorRuntimeState& runtime, CalculationResult& result) {
            if (!context.historicalView->hasField(column.toStdString())) {
                const QString errorMessage = QString("缓存数据集缺少字段 %1，无法计算规模因子").arg(column);
                result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                return;
            }

            const auto crossSection = context.historicalView->getCrossSection(runtime.effectiveDate.toStdString(), column.toStdString(), context.symbols);
            for (const auto& [symbol, rawValue] : crossSection) {
                if (rawValue <= 0.0) {
                    continue;
                }
                result.values[symbol] = scoreFromRawValue(rawValue);
            }
        },
        [](const CommonFactorRuntimeState&, CalculationResult& result) {
            std::vector<double> finiteValues;
            finiteValues.reserve(result.values.size());
            for (const auto& [symbol, value] : result.values) {
                Q_UNUSED(symbol);
                if (std::isfinite(value)) {
                    finiteValues.push_back(value);
                }
            }

            if (finiteValues.size() >= 16) {
                const double lower = BaseFactor::calculatePercentileValue(finiteValues, 0.05);
                const double upper = BaseFactor::calculatePercentileValue(finiteValues, 0.95);
                if (upper > lower) {
                    for (auto& [symbol, value] : result.values) {
                        Q_UNUSED(symbol);
                        value = (std::max)(lower, (std::min)(upper, value));
                    }
                }
            }
        },
        [this](const CommonFactorRuntimeState&, CalculationResult& result) {
            result.metadata.set("sizeMetric", json_helper::toJsonValue(params_.sizeMetric));
            result.metadata.set("logTransform", json_helper::toJsonValue(params_.logTransform));
            result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
        });
}

DataRequirements SizeFactor::getDataRequirements() const {
    DataRequirements req;
    const auto appendUnique = [&req](const std::string& field) {
        if (field.empty()) {
            return;
        }
        if (std::find(req.requiredFields.begin(), req.requiredFields.end(), field) == req.requiredFields.end()) {
            req.requiredFields.push_back(field);
        }
    };

    appendUnique(selectedColumn().toStdString());
    if (params_.neutralizationEnabled) {
        appendUnique("industry_code");
        appendUnique("market_cap");
    }
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
    dataRequirements_.requiredFields = getDataRequirements().requiredFields;
}

} // namespace factor