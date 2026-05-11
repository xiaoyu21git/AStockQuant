#include "domain/factor/include/QualityFactor.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include "ui/bridge/include/DataFetchFieldContractUtils.h"

#include <QString>

#include <unordered_set>

namespace factor {

namespace {

QString normalizedMetric(const std::string& metric)
{
    const QString normalized = QString::fromStdString(metric).trimmed().toLower();
    if (normalized == QString::fromUtf8("净资产收益率") || normalized == QString(factor::bridge::FinancialFieldKeys::ROE)) {
        return QString(factor::bridge::FinancialFieldKeys::ROE);
    }
    if (normalized == QString::fromUtf8("总资产收益率") || normalized == QString(factor::bridge::FinancialFieldKeys::ROA)) {
        return QString(factor::bridge::FinancialFieldKeys::ROA);
    }
    if (normalized == QString::fromUtf8("营业利润率") || normalized == QString(factor::bridge::FinancialFieldKeys::OPERATING_MARGIN)) {
        return QString(factor::bridge::FinancialFieldKeys::OPERATING_MARGIN);
    }
    if (normalized == QString::fromUtf8("毛利率")
            || normalized == QString(factor::bridge::FinancialFieldKeys::GROSS_MARGIN)
            || normalized == QString(factor::bridge::FinancialFieldKeys::PROFIT_MARGIN)) {
        return QString(factor::bridge::FinancialFieldKeys::GROSS_MARGIN);
    }
    if (normalized == QStringLiteral("earnings_quality") || normalized == QStringLiteral("net_profit_to_equity") || normalized == QString::fromUtf8("收益质量")) {
        return QStringLiteral("earnings_quality");
    }
    return normalized;
}

QString resolveMetricColumn(const QString& metric)
{
    if (metric == QString(factor::bridge::FinancialFieldKeys::ROE)) {
        return QString(factor::bridge::FinancialFieldKeys::ROE);
    }
    if (metric == QString(factor::bridge::FinancialFieldKeys::ROA)) {
        return QString(factor::bridge::FinancialFieldKeys::ROA);
    }
    if (metric == QString(factor::bridge::FinancialFieldKeys::GROSS_MARGIN)
            || metric == QString(factor::bridge::FinancialFieldKeys::OPERATING_MARGIN)) {
        return QString(factor::bridge::FinancialFieldKeys::PROFIT_MARGIN);
    }
    return QString();
}

double normalizeThreshold(double threshold)
{
    return threshold > 1.0 ? threshold / 100.0 : threshold;
}

}

QualityFactor::QualityFactor() {
    factorType_ = "质量因子";
}

CalculationResult QualityFactor::calculate(const CalculationContext& context) {
    const QString metric = normalizedMetric(params_.metric);
    const double qualityThreshold = normalizeThreshold(params_.qualityThreshold);

    QStringList requiredFields;
    if (metric == QStringLiteral("earnings_quality")) {
        requiredFields = {QString(factor::bridge::FinancialFieldKeys::NET_PROFIT), QString(factor::bridge::FinancialFieldKeys::EQUITY)};
    } else {
        const QString fieldName = resolveMetricColumn(metric);
        if (fieldName.isEmpty()) {
            auto result = CalculationResult::createError(
                QStringLiteral("质量因子 HistoricalView 回测不支持指标 %1").arg(metric).toStdString());
            result.date = context.date;
            result.calculationId = foundation::utils::Uuid::generate_v4();
            result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
            return result;
        }
        requiredFields = {fieldName};
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
        requiredFields,
        [this, &context, &requiredFields, metric, qualityThreshold](const CommonFactorRuntimeState& runtime,
                                                                    CalculationResult& result) {
            auto requireField = [&](const char* fieldName) {
                if (!context.historicalView->hasField(fieldName)) {
                    const std::string error = QStringLiteral("质量因子 HistoricalView 回测缺少字段 %1")
                        .arg(QString::fromUtf8(fieldName))
                        .toStdString();
                    result.dataStatus = CalculationResult::createError(error).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(error));
                    return false;
                }
                return true;
            };

            for (const QString& fieldName : requiredFields) {
                if (!requireField(fieldName.toUtf8().constData())) {
                    return;
                }
            }

            if (metric == QStringLiteral("earnings_quality")) {
                const auto netProfitMap = context.historicalView->getCrossSection(runtime.effectiveDate.toStdString(), "net_profit", context.symbols);
                const auto equityMap = context.historicalView->getCrossSection(runtime.effectiveDate.toStdString(), "equity", context.symbols);
                for (const auto& [symbol, netProfit] : netProfitMap) {
                    const auto equityIt = equityMap.find(symbol);
                    if (equityIt == equityMap.end()) {
                        continue;
                    }
                    if (netProfit <= 0.0 || equityIt->second <= 0.0) {
                        continue;
                    }
                    const double factorValue = netProfit / equityIt->second;
                    if (factorValue >= qualityThreshold) {
                        result.values[symbol] = factorValue;
                    }
                }
            } else {
                const QString fieldName = requiredFields.front();
                const auto crossSection = context.historicalView->getCrossSection(runtime.effectiveDate.toStdString(), fieldName.toStdString(), context.symbols);
                for (const auto& [symbol, factorValue] : crossSection) {
                    if (factorValue > 0.0 && factorValue >= qualityThreshold) {
                        result.values[symbol] = factorValue;
                    }
                }
            }

            if (result.values.empty()) {
                result.dataStatus.availability = DataAvailability::UNAVAILABLE;
                result.dataStatus.coverage = 0.0;
                result.dataStatus.message = "未查询到满足条件的质量因子数据";
                result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
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
        [this, &context, metric, qualityThreshold](const CommonFactorRuntimeState&, CalculationResult& result) {
            if (!result.values.empty()) {
                result.values = handleOutliers(applyBoundaryRules(result.values, context));
            }
            result.metadata.set("metric", json_helper::toJsonValue(metric.toStdString()));
            result.metadata.set("qualityThreshold", json_helper::toJsonValue(qualityThreshold));
            result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
        });
}

DataRequirements QualityFactor::getDataRequirements() const {
    DataRequirements req;
    const auto appendUnique = [&req](const std::string& field) {
        if (std::find(req.requiredFields.begin(), req.requiredFields.end(), field) == req.requiredFields.end()) {
            req.requiredFields.push_back(field);
        }
    };
    const QString metric = normalizedMetric(params_.metric);
    if (metric == "roe") {
        appendUnique("roe");
    } else if (metric == "roa") {
        appendUnique("roa");
    } else if (metric == "gross_margin" || metric == "operating_margin") {
        appendUnique("profit_margin");
    } else {
        appendUnique("net_profit");
        appendUnique("equity");
    }
    if (params_.neutralizationEnabled) {
        appendUnique("industry_code");
        appendUnique("market_cap");
    }
    return req;
}

BoundaryRules QualityFactor::getBoundaryRules() const {
    BoundaryRules rules;
    rules.minDataPoints = 1;
    return rules;
}

std::shared_ptr<QualityFactor> QualityFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker) {

    auto factor = std::make_shared<QualityFactor>();
    factor->dataChecker_ = dataChecker;
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

void QualityFactor::loadConfig(const foundation::json::JsonFacade& config) {
    BaseFactor::loadConfig(config);
    if (config.has("calculation")) {
        const auto calculation = config.get("calculation");
        params_.fromJson(calculation);
        params_.metric = normalizedMetric(params_.metric).toStdString();
    }
    dataRequirements_.requiredFields = getDataRequirements().requiredFields;
}

} // namespace factor