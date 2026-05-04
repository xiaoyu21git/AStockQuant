#include "domain/factor/include/QualityFactor.h"
#include "domain/factor/include/FactorInstanceManager.h"

#include <QDate>
#include <QString>

#include <unordered_set>

namespace factor {

namespace {

QString normalizedMetric(const std::string& metric)
{
    const QString normalized = QString::fromStdString(metric).trimmed().toLower();
    if (normalized == QString::fromUtf8("净资产收益率") || normalized == QStringLiteral("roe")) {
        return QStringLiteral("roe");
    }
    if (normalized == QString::fromUtf8("总资产收益率") || normalized == QStringLiteral("roa")) {
        return QStringLiteral("roa");
    }
    if (normalized == QString::fromUtf8("营业利润率") || normalized == QStringLiteral("operating_margin")) {
        return QStringLiteral("operating_margin");
    }
    if (normalized == QString::fromUtf8("毛利率") || normalized == QStringLiteral("gross_margin") || normalized == QStringLiteral("profit_margin")) {
        return QStringLiteral("gross_margin");
    }
    if (normalized == QStringLiteral("earnings_quality") || normalized == QStringLiteral("net_profit_to_equity") || normalized == QString::fromUtf8("收益质量")) {
        return QStringLiteral("earnings_quality");
    }
    return normalized;
}

QString resolveMetricColumn(const QString& metric)
{
    if (metric == "roe") {
        return "roe";
    }
    if (metric == "roa") {
        return "roa";
    }
    if (metric == "gross_margin" || metric == "operating_margin") {
        return "profit_margin";
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
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(
            context,
            QStringLiteral("已移除质量因子运行期数据库取数路径，请由引擎提供 HistoricalView").toStdString());
    }

    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用缓存数据集";

    const QString metric = normalizedMetric(params_.metric);
    const double qualityThreshold = normalizeThreshold(params_.qualityThreshold);

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

    if (metric == QStringLiteral("earnings_quality")) {
        if (!requireField("net_profit") || !requireField("equity")) {
            return result;
        }

        const auto netProfitMap = context.historicalView->getCrossSection(context.date, "net_profit", context.symbols);
        const auto equityMap = context.historicalView->getCrossSection(context.date, "equity", context.symbols);
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
        const QString fieldName = resolveMetricColumn(metric);
        if (fieldName.isEmpty()) {
            const std::string error = QStringLiteral("质量因子 HistoricalView 回测不支持指标 %1")
                .arg(metric)
                .toStdString();
            result.dataStatus = CalculationResult::createError(error).dataStatus;
            result.metadata.set("error", json_helper::toJsonValue(error));
            return result;
        }
        if (!requireField(fieldName.toUtf8().constData())) {
            return result;
        }

        const auto crossSection = context.historicalView->getCrossSection(context.date, fieldName.toStdString(), context.symbols);
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
        return result;
    }

    result.values = handleOutliers(applyBoundaryRules(result.values, context));
    result.metadata.set("metric", json_helper::toJsonValue(metric.toStdString()));
    result.metadata.set("timeframe", json_helper::toJsonValue(params_.timeframe));
    result.metadata.set("qualityThreshold", json_helper::toJsonValue(qualityThreshold));
    result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
    return result;
}

DataRequirements QualityFactor::getDataRequirements() const {
    DataRequirements req;
    const QString metric = normalizedMetric(params_.metric);
    if (metric == "roe") {
        req.requiredFields = {"roe"};
    } else if (metric == "roa") {
        req.requiredFields = {"roa"};
    } else if (metric == "gross_margin" || metric == "operating_margin") {
        req.requiredFields = {"profit_margin"};
    } else {
        req.requiredFields = {"net_profit", "equity"};
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
        if (params_.metric.empty() && calculation.has("qualityMetrics")) {
            const auto metrics = calculation.get("qualityMetrics");
            if (metrics.isArray() && metrics.size() > 0) {
                params_.metric = metrics.at(0).asString();
            }
        }
        params_.metric = normalizedMetric(params_.metric).toStdString();
    }
    dataRequirements_.requiredFields = getDataRequirements().requiredFields;
}

} // namespace factor