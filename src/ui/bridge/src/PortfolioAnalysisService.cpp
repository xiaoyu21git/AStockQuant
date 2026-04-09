#include "PortfolioAnalysisService.h"

#include "FactorService.h"
#include "MarketDataService.h"
#include "PositionAccountService.h"
#include "RiskMonitorService.h"
#include "StrategyService.h"
#include "PortfolioExecutionPlanUtils.h"
#include "../include/StrategyStructureResolvers.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutexLocker>
#include <QHash>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

struct PortfolioFactorAllocation {
    QString factorId;
    QString displayName;
    QString category;
    double weight{0.0};
    double correlation{0.0};
    double icValue{0.0};
    double irValue{0.0};
    double turnoverRate{0.0};
};

int stableBucketIndex(const QString& text, int bucketCount);
QVariantMap buildEstimatedExposureState(const QList<PortfolioFactorAllocation>& allocations, double totalWeight);

double clampValue(double value, double minimum, double maximum)
{
    return (std::max)(minimum, (std::min)(value, maximum));
}

QString currentTimeLabel()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
}

QVariantMap createNotification(const QString& type, const QString& text, const QString& action = QStringLiteral("查看"))
{
    QVariantMap result;
    result.insert(QStringLiteral("type"), type);
    result.insert(QStringLiteral("text"), text);
    result.insert(QStringLiteral("time"), currentTimeLabel());
    result.insert(QStringLiteral("action"), action);
    return result;
}

bool hasObjectData(const QVariantMap& value)
{
    return !value.isEmpty();
}

double numberOrDefault(const QVariant& value, double fallback)
{
    bool ok = false;
    const double numericValue = value.toDouble(&ok);
    return ok ? numericValue : fallback;
}

double normalizePercentMetric(const QVariant& value)
{
    bool ok = false;
    const double numericValue = value.toDouble(&ok);
    if (!ok || !std::isfinite(numericValue)) {
        return 0.0;
    }
    return std::fabs(numericValue) <= 1.0 ? numericValue * 100.0 : numericValue;
}

QVariantMap emptySectorExposure()
{
    QVariantMap result;
    result.insert(QStringLiteral("银行"), 0.0);
    result.insert(QStringLiteral("消费"), 0.0);
    result.insert(QStringLiteral("医药"), 0.0);
    result.insert(QStringLiteral("科技"), 0.0);
    return result;
}

QVariantMap emptyStyleExposure()
{
    QVariantMap result;
    result.insert(QStringLiteral("市值"), 0.0);
    result.insert(QStringLiteral("动量"), 0.0);
    result.insert(QStringLiteral("价值"), 0.0);
    result.insert(QStringLiteral("波动率"), 0.0);
    return result;
}

QVariantMap defaultMetrics()
{
    QVariantMap result;
    result.insert(QStringLiteral("annualReturn"), 0.0);
    result.insert(QStringLiteral("sharpeRatio"), 0.0);
    result.insert(QStringLiteral("maxDrawdown"), 0.0);
    return result;
}

QVariantMap defaultSnapshot(const QString& status = QStringLiteral("idle"), const QString& error = QString())
{
    QVariantMap result;
    result.insert(QStringLiteral("status"), status);
    result.insert(QStringLiteral("positions"), QVariantList{});
    result.insert(QStringLiteral("diagnostics"), QVariantMap{});
    if (!error.isEmpty()) {
        result.insert(QStringLiteral("error"), error);
    }
    return result;
}

QVariantMap variantMapValue(const QVariant& value)
{
    return value.canConvert<QVariantMap>() ? value.toMap() : QVariantMap{};
}

void mergeConfiguredMap(QVariantMap& target, const QVariantMap& source)
{
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        const QVariant& value = it.value();
        if (!value.isValid() || value.isNull()) {
            continue;
        }
        if (value.typeId() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
            continue;
        }
        target.insert(it.key(), value);
    }
}

bridge::config::StrategyStructureResolution resolveStrategyStructures(const QVariantMap& strategy)
{
    const bridge::config::StrategyStructureResolverSet resolverSet;
    return resolverSet.resolve(strategy);
}

QVariantMap buildResolvedStrategyConfig(const QVariantMap& strategy, const QVariantMap& options)
{
    const bridge::config::StrategyStructureResolution resolution = resolveStrategyStructures(strategy);

    QVariantMap resolvedConfig = resolution.strategyView;
    mergeConfiguredMap(resolvedConfig, resolution.backtestAssumptions);
    mergeConfiguredMap(resolvedConfig, resolution.executionPolicy);
    mergeConfiguredMap(resolvedConfig, resolution.ruleProfile);
    mergeConfiguredMap(resolvedConfig, resolution.strategyScopeContext);
    mergeConfiguredMap(resolvedConfig, options);
    return resolvedConfig;
}

QVariant firstConfiguredValue(const QVariantMap& primary,
                             const QVariantMap& secondary,
                             const QStringList& keys)
{
    for (const QString& key : keys) {
        const QVariant primaryValue = primary.value(key);
        if (primaryValue.isValid() && !primaryValue.isNull()) {
            if (primaryValue.typeId() != QMetaType::QString || !primaryValue.toString().trimmed().isEmpty()) {
                return primaryValue;
            }
        }

        const QVariant secondaryValue = secondary.value(key);
        if (secondaryValue.isValid() && !secondaryValue.isNull()) {
            if (secondaryValue.typeId() != QMetaType::QString || !secondaryValue.toString().trimmed().isEmpty()) {
                return secondaryValue;
            }
        }
    }

    return {};
}

int normalizedPositiveExecutionCount(const QVariant& value)
{
    bool ok = false;
    const int numericValue = value.toInt(&ok);
    return ok && numericValue > 0 ? numericValue : 0;
}

double normalizedPositiveExecutionLimit(const QVariant& value)
{
    const double numericValue = numberOrDefault(value, 0.0);
    return numericValue > 0.0 ? numericValue : 0.0;
}

bool normalizedBooleanExecutionOption(const QVariant& value, bool fallback = false)
{
    if (!value.isValid() || value.isNull()) {
        return fallback;
    }

    if (value.typeId() == QMetaType::Bool) {
        return value.toBool();
    }

    const QString normalized = value.toString().trimmed().toLower();
    if (normalized.isEmpty()) {
        return fallback;
    }

    return normalized == QStringLiteral("true")
        || normalized == QStringLiteral("1")
        || normalized == QStringLiteral("yes")
        || normalized == QStringLiteral("y");
}

QVariantMap resolveExecutionBatchOptions(const QVariantMap& resolvedConfig)
{
    const QVariantMap batchPolicy = variantMapValue(resolvedConfig.value(QStringLiteral("batchExecution")));

    const int maxBatchOrders = normalizedPositiveExecutionCount(firstConfiguredValue(
        batchPolicy,
        resolvedConfig,
        {QStringLiteral("maxBatchOrders"), QStringLiteral("batchOrderLimit")}));

    double maxBatchNotionalWan = normalizedPositiveExecutionLimit(firstConfiguredValue(
        batchPolicy,
        resolvedConfig,
        {QStringLiteral("maxBatchNotionalWan"), QStringLiteral("batchNotionalLimitWan")}));
    const double maxBatchNotionalAbsolute = normalizedPositiveExecutionLimit(firstConfiguredValue(
        batchPolicy,
        resolvedConfig,
        {QStringLiteral("maxBatchNotional"), QStringLiteral("batchNotionalLimit")}));
    const double maxBatchNotional = maxBatchNotionalAbsolute > 0.0
        ? maxBatchNotionalAbsolute
        : maxBatchNotionalWan * 10000.0;
    if (maxBatchNotionalWan <= 0.0 && maxBatchNotional > 0.0) {
        maxBatchNotionalWan = maxBatchNotional / 10000.0;
    }

    const bool waitPreviousBatchFilled = normalizedBooleanExecutionOption(
        firstConfiguredValue(batchPolicy, resolvedConfig, {QStringLiteral("waitPreviousBatchFilled")}),
        true);
    const bool pauseOnConflict = normalizedBooleanExecutionOption(
        firstConfiguredValue(batchPolicy, resolvedConfig, {QStringLiteral("pauseOnConflict")}),
        false);
    const bool pauseOnAbnormalReject = normalizedBooleanExecutionOption(
        firstConfiguredValue(batchPolicy, resolvedConfig, {QStringLiteral("pauseOnAbnormalReject")}),
        false);
    const int manualCheckpointBatchIndex = normalizedPositiveExecutionCount(firstConfiguredValue(
        batchPolicy,
        resolvedConfig,
        {QStringLiteral("manualCheckpointBatchIndex")}));
    const bool requireManualCheckpoint = manualCheckpointBatchIndex > 0 || normalizedBooleanExecutionOption(
        firstConfiguredValue(batchPolicy, resolvedConfig, {QStringLiteral("requireManualCheckpoint")}),
        false);

    return QVariantMap{{QStringLiteral("maxBatchOrders"), maxBatchOrders},
                       {QStringLiteral("waitPreviousBatchFilled"), waitPreviousBatchFilled},
                       {QStringLiteral("pauseOnConflict"), pauseOnConflict},
                       {QStringLiteral("pauseOnAbnormalReject"), pauseOnAbnormalReject},
                       {QStringLiteral("requireManualCheckpoint"), requireManualCheckpoint},
                       {QStringLiteral("manualCheckpointBatchIndex"), manualCheckpointBatchIndex},
                       {QStringLiteral("maxBatchNotional"), maxBatchNotional},
                       {QStringLiteral("maxBatchNotionalWan"), maxBatchNotionalWan}};
}

double normalizedPercentValue(const QVariant& value, double fallback)
{
    bool ok = false;
    const double numericValue = value.toDouble(&ok);
    if (!ok || !std::isfinite(numericValue) || numericValue <= 0.0) {
        return fallback;
    }
    return numericValue <= 1.0 ? numericValue * 100.0 : numericValue;
}

QVariantList parseRawAllocationList(const QVariant& rawAllocations)
{
    if (!rawAllocations.isValid() || rawAllocations.isNull()) {
        return {};
    }

    if (rawAllocations.typeId() == QMetaType::QString) {
        const QString jsonText = rawAllocations.toString().trimmed();
        if (jsonText.isEmpty()) {
            return {};
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
            return {};
        }

        return document.array().toVariantList();
    }

    return rawAllocations.toList();
}

QList<PortfolioFactorAllocation> parseAllocations(const QVariantMap& strategy,
                                                  const QVariantMap& resolvedConfig)
{
    QVariant rawAllocations = firstConfiguredValue(
        resolvedConfig,
        strategy,
        {QStringLiteral("portfolio_allocations_json"), QStringLiteral("factor_allocations"), QStringLiteral("allocations")});

    if ((!rawAllocations.isValid() || rawAllocations.isNull()) && !resolvedConfig.isEmpty()) {
        const QVariantMap strategyParameters = variantMapValue(strategy.value(QStringLiteral("parameters")));
        rawAllocations = firstConfiguredValue(
            resolvedConfig,
            strategyParameters,
            {QStringLiteral("portfolio_allocations_json"), QStringLiteral("factor_allocations"), QStringLiteral("allocations")});
    }

    const QVariantList allocationList = parseRawAllocationList(rawAllocations);

    QList<PortfolioFactorAllocation> allocations;
    allocations.reserve(allocationList.size());
    for (const QVariant& entry : allocationList) {
        const QVariantMap map = entry.toMap();
        if (map.isEmpty()) {
            continue;
        }

        const QString factorId = map.value(QStringLiteral("factor_id"), map.value(QStringLiteral("factorId"))).toString().trimmed();
        if (factorId.isEmpty()) {
            continue;
        }

        PortfolioFactorAllocation allocation;
        allocation.factorId = factorId;
        allocation.displayName = map.value(QStringLiteral("display_name"), map.value(QStringLiteral("displayName"), factorId)).toString().trimmed();
        allocation.category = map.value(QStringLiteral("category"), QStringLiteral("综合类")).toString().trimmed();
        allocation.weight = numberOrDefault(map.value(QStringLiteral("weight")), 0.0);
        allocation.correlation = numberOrDefault(map.value(QStringLiteral("correlation")), 0.0);
        allocation.icValue = numberOrDefault(map.value(QStringLiteral("ic_value"), map.value(QStringLiteral("icValue"))), 0.0);
        allocation.irValue = numberOrDefault(map.value(QStringLiteral("ir_value"), map.value(QStringLiteral("irValue"))), 0.0);
        allocation.turnoverRate = numberOrDefault(map.value(QStringLiteral("turnover_rate"), map.value(QStringLiteral("turnoverRate"))), 0.0);
        allocations.push_back(allocation);
    }

    return allocations;
}

double totalAllocationWeight(const QList<PortfolioFactorAllocation>& allocations)
{
    double totalWeight = 0.0;
    for (const PortfolioFactorAllocation& allocation : allocations) {
        totalWeight += allocation.weight;
    }
    return totalWeight;
}

QString resolvePositionSizingMethod(const QVariantMap& strategy, const QVariantMap& options)
{
    const QString rawValue = firstConfiguredValue(
        options,
        QVariantMap{},
        {QStringLiteral("positionSizingMethod"), QStringLiteral("position_sizing_method")}).toString().trimmed().toLower();

    if (rawValue == QStringLiteral("kelly")
            || rawValue == QStringLiteral("riskparity")
            || rawValue == QStringLiteral("risk_parity")
            || rawValue == QStringLiteral("equalweight")
            || rawValue == QStringLiteral("equal_weight")) {
        return rawValue;
    }

    return QStringLiteral("fixed");
}

double resolveMaxWeightPercent(const QVariantMap& strategy,
                               const QVariantMap& options,
                               int factorCount)
{
    const QVariant configuredValue = firstConfiguredValue(
        options,
        QVariantMap{},
        {QStringLiteral("maxWeightPercent"), QStringLiteral("maxPositionPercent"), QStringLiteral("maxSinglePositionRatio"), QStringLiteral("position_size"), QStringLiteral("positionSize")});

    const double defaultMaxWeight = factorCount > 0
        ? clampValue((100.0 / static_cast<double>(factorCount)) * 1.8, 22.0, 38.0)
        : 35.0;
    return clampValue(normalizedPercentValue(configuredValue, defaultMaxWeight), 5.0, 80.0);
}

double resolveMinWeightPercent(const QVariantMap& strategy,
                               const QVariantMap& options,
                               int factorCount)
{
    const QVariant configuredValue = firstConfiguredValue(
        options,
        QVariantMap{},
        {QStringLiteral("minWeightPercent"), QStringLiteral("minPositionPercent"), QStringLiteral("minSinglePositionRatio")});

    double defaultMinWeight = 3.0;
    if (factorCount <= 4) {
        defaultMinWeight = 10.0;
    } else if (factorCount <= 8) {
        defaultMinWeight = 6.0;
    }

    return clampValue(normalizedPercentValue(configuredValue, defaultMinWeight), 0.5, 25.0);
}

double scoreAllocationForMethod(const PortfolioFactorAllocation& allocation,
                                const QString& positionSizingMethod,
                                int categoryCount)
{
    const double alphaEdge = std::fabs(allocation.icValue) * 100.0 + std::fabs(allocation.irValue) * 10.0;
    const double turnoverReward = (std::max)(0.0, 30.0 - allocation.turnoverRate) * 0.2;
    const double diversificationReward = (1.0 - std::fabs(allocation.correlation)) * 25.0;
    const double categoryReward = categoryCount > 0 ? (10.0 / static_cast<double>(categoryCount)) : 0.0;

    if (positionSizingMethod == QStringLiteral("equalweight") || positionSizingMethod == QStringLiteral("equal_weight")) {
        return 1.0;
    }

    if (positionSizingMethod == QStringLiteral("riskparity") || positionSizingMethod == QStringLiteral("risk_parity")) {
        const double riskPenalty = 1.0 + std::fabs(allocation.correlation) * 1.4 + allocation.turnoverRate / 45.0;
        return (alphaEdge * 0.55 + diversificationReward + categoryReward) / (std::max)(0.35, riskPenalty);
    }

    if (positionSizingMethod == QStringLiteral("kelly")) {
        const double edge = (std::max)(0.01, std::fabs(allocation.icValue) * 80.0 + std::fabs(allocation.irValue) * 4.0 + categoryReward);
        const double riskPenalty = (std::max)(0.4, 1.0 + std::fabs(allocation.correlation) * 1.8 + allocation.turnoverRate / 35.0);
        return edge / riskPenalty;
    }

    return alphaEdge + turnoverReward + diversificationReward + categoryReward;
}

std::vector<double> normalizeWeightsWithBounds(const std::vector<double>& rawWeights,
                                               double minWeightPercent,
                                               double maxWeightPercent)
{
    std::vector<double> boundedWeights = rawWeights;
    if (boundedWeights.empty()) {
        return boundedWeights;
    }

    const double factorCount = static_cast<double>(boundedWeights.size());
    if (minWeightPercent * factorCount > 100.0) {
        minWeightPercent = 100.0 / factorCount;
    }
    if (maxWeightPercent * factorCount < 100.0) {
        maxWeightPercent = 100.0;
    }

    for (double& weight : boundedWeights) {
        weight = clampValue(weight, minWeightPercent, maxWeightPercent);
    }

    for (int iteration = 0; iteration < 24; ++iteration) {
        double currentTotal = 0.0;
        for (double weight : boundedWeights) {
            currentTotal += weight;
        }

        const double diff = 100.0 - currentTotal;
        if (std::fabs(diff) < 0.01) {
            break;
        }

        std::vector<double> capacities(boundedWeights.size(), 0.0);
        double totalCapacity = 0.0;
        for (std::size_t index = 0; index < boundedWeights.size(); ++index) {
            capacities[index] = diff > 0.0
                ? (std::max)(0.0, maxWeightPercent - boundedWeights[index])
                : (std::max)(0.0, boundedWeights[index] - minWeightPercent);
            totalCapacity += capacities[index];
        }

        if (totalCapacity <= 0.0) {
            break;
        }

        for (std::size_t index = 0; index < boundedWeights.size(); ++index) {
            if (capacities[index] <= 0.0) {
                continue;
            }

            const double adjustment = diff * (capacities[index] / totalCapacity);
            boundedWeights[index] = diff > 0.0
                ? (std::min)(maxWeightPercent, boundedWeights[index] + adjustment)
                : (std::max)(minWeightPercent, boundedWeights[index] + adjustment);
        }
    }

    double normalizedTotal = 0.0;
    for (double weight : boundedWeights) {
        normalizedTotal += weight;
    }
    if (normalizedTotal > 0.0) {
        const double scale = 100.0 / normalizedTotal;
        for (double& weight : boundedWeights) {
            weight = clampValue(weight * scale, minWeightPercent, maxWeightPercent);
        }
    }

    return boundedWeights;
}

QVariantList buildOptimizedAllocationList(const QList<PortfolioFactorAllocation>& allocations,
                                          const std::vector<double>& optimizedWeights)
{
    QVariantList result;
    result.reserve(allocations.size());
    for (int index = 0; index < allocations.size(); ++index) {
        QVariantMap allocationMap;
        allocationMap.insert(QStringLiteral("factor_id"), allocations[index].factorId);
        allocationMap.insert(QStringLiteral("display_name"), allocations[index].displayName);
        allocationMap.insert(QStringLiteral("category"), allocations[index].category);
        allocationMap.insert(QStringLiteral("weight"), index < static_cast<int>(optimizedWeights.size()) ? optimizedWeights[static_cast<std::size_t>(index)] : allocations[index].weight);
        allocationMap.insert(QStringLiteral("correlation"), allocations[index].correlation);
        allocationMap.insert(QStringLiteral("ic_value"), allocations[index].icValue);
        allocationMap.insert(QStringLiteral("ir_value"), allocations[index].irValue);
        allocationMap.insert(QStringLiteral("turnover_rate"), allocations[index].turnoverRate);
        result.append(allocationMap);
    }
    return result;
}

QStringList sectorBuckets()
{
    return {QStringLiteral("银行"), QStringLiteral("消费"), QStringLiteral("医药"), QStringLiteral("科技")};
}

double sectorAffinity(const PortfolioFactorAllocation& allocation, const QString& sector)
{
    const QStringList sectors = sectorBuckets();
    const int primaryBucket = stableBucketIndex(
        allocation.factorId.isEmpty() ? allocation.displayName : allocation.factorId,
        sectors.size());
    const int secondaryBucket = (primaryBucket + 1) % sectors.size();

    if (sector == sectors[primaryBucket]) {
        return 1.0;
    }
    if (sector == sectors[secondaryBucket]) {
        return 0.45;
    }
    return 0.0;
}

double styleAffinity(const PortfolioFactorAllocation& allocation, const QString& style)
{
    const QString category = allocation.category.trimmed();

    if (style == QStringLiteral("动量")) {
        if (category == QStringLiteral("动量类")) return 1.0;
        if (category == QStringLiteral("情绪类")) return 0.65;
        if (category == QStringLiteral("综合类")) return 0.3;
        return 0.08;
    }

    if (style == QStringLiteral("价值")) {
        if (category == QStringLiteral("价值类")) return 1.0;
        if (category == QStringLiteral("质量类")) return 0.55;
        if (category == QStringLiteral("综合类")) return 0.3;
        return 0.08;
    }

    if (style == QStringLiteral("市值")) {
        if (category == QStringLiteral("质量类")) return 0.85;
        if (category == QStringLiteral("价值类")) return 0.55;
        if (category == QStringLiteral("流动性类")) return 0.7;
        if (category == QStringLiteral("综合类")) return 0.35;
        return 0.12;
    }

    if (style == QStringLiteral("波动率")) {
        if (category == QStringLiteral("情绪类")) return 0.8;
        if (category == QStringLiteral("流动性类")) return 0.7;
        if (category == QStringLiteral("动量类")) return 0.45;
        if (category == QStringLiteral("综合类")) return 0.3;
        return 0.12;
    }

    return 0.0;
}

double targetValueForFocus(const QString& focusType, const QString& focusKey)
{
    if (focusType == QStringLiteral("sector")) {
        return 0.25;
    }

    if (focusType == QStringLiteral("style")) {
        if (focusKey == QStringLiteral("市值")) return 1.0;
        if (focusKey == QStringLiteral("动量")) return 0.8;
        if (focusKey == QStringLiteral("价值")) return 0.6;
        if (focusKey == QStringLiteral("波动率")) return 1.0;
    }

    return 0.0;
}

double currentExposureForFocus(const QVariantMap& estimatedExposures,
                               const QString& focusType,
                               const QString& focusKey)
{
    if (focusType == QStringLiteral("sector")) {
        return numberOrDefault(estimatedExposures.value(QStringLiteral("sector")).toMap().value(focusKey), 0.0);
    }
    if (focusType == QStringLiteral("style")) {
        return numberOrDefault(estimatedExposures.value(QStringLiteral("style")).toMap().value(focusKey), 0.0);
    }
    return 0.0;
}

double affinityForFocus(const PortfolioFactorAllocation& allocation,
                        const QString& focusType,
                        const QString& focusKey)
{
    if (focusType == QStringLiteral("sector")) {
        return sectorAffinity(allocation, focusKey);
    }
    if (focusType == QStringLiteral("style")) {
        return styleAffinity(allocation, focusKey);
    }
    return 0.0;
}

QString exposureAdjustActionLabel(const QString& focusType,
                                  const QString& focusKey,
                                  bool reduceFocus)
{
    if (focusType == QStringLiteral("sector")) {
        return reduceFocus
            ? QStringLiteral("已降低%1集中度").arg(focusKey)
            : QStringLiteral("已提升%1配置").arg(focusKey);
    }

    return reduceFocus
        ? QStringLiteral("已压降%1暴露").arg(focusKey)
        : QStringLiteral("已提升%1暴露").arg(focusKey);
}

QVariantMap buildExposureAdjustmentResult(const QList<PortfolioFactorAllocation>& allocations,
                                          const QString& focusType,
                                          const QString& focusKey,
                                          const QVariantMap& strategy,
                                          const QVariantMap& options)
{
    if (allocations.isEmpty()) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), QStringLiteral("当前组合为空，无法调整暴露")}};
    }

    const double totalWeight = totalAllocationWeight(allocations);
    const QVariantMap estimatedExposures = buildEstimatedExposureState(allocations, totalWeight);
    const double currentValue = currentExposureForFocus(estimatedExposures, focusType, focusKey);
    const double targetValue = targetValueForFocus(focusType, focusKey);
    const bool reduceFocus = currentValue > targetValue;

    const QVariantMap resolvedStrategyConfig = buildResolvedStrategyConfig(strategy, options);

    const double minWeightPercent = resolveMinWeightPercent(strategy, resolvedStrategyConfig, allocations.size());
    const double maxWeightPercent = resolveMaxWeightPercent(strategy, resolvedStrategyConfig, allocations.size());

    const double rawStrength = focusType == QStringLiteral("sector")
        ? std::fabs(currentValue - targetValue) * 1.6 + 0.12
        : std::fabs(currentValue - targetValue) * 0.45 + 0.12;
    const double tiltStrength = clampValue(rawStrength, 0.12, focusType == QStringLiteral("sector") ? 0.32 : 0.28);

    std::vector<double> adjustedWeights;
    adjustedWeights.reserve(static_cast<std::size_t>(allocations.size()));
    const double defaultWeight = allocations.isEmpty() ? 0.0 : 100.0 / static_cast<double>(allocations.size());

    for (const PortfolioFactorAllocation& allocation : allocations) {
        const double baseWeight = allocation.weight > 0.0 ? allocation.weight : defaultWeight;
        const double affinity = affinityForFocus(allocation, focusType, focusKey);

        double multiplier = 1.0;
        if (reduceFocus) {
            multiplier = affinity > 0.0
                ? 1.0 - tiltStrength * (0.75 + affinity * 0.4)
                : 1.0 + tiltStrength * 0.12;
        } else {
            multiplier = affinity > 0.0
                ? 1.0 + tiltStrength * affinity
                : 1.0 - tiltStrength * 0.18;
        }

        adjustedWeights.push_back((std::max)(0.01, baseWeight * multiplier));
    }

    const std::vector<double> normalizedWeights = normalizeWeightsWithBounds(adjustedWeights, minWeightPercent, maxWeightPercent);
    const QVariantList adjustedAllocations = buildOptimizedAllocationList(allocations, normalizedWeights);

    QList<PortfolioFactorAllocation> adjustedAllocationRows = allocations;
    for (int index = 0; index < adjustedAllocationRows.size() && index < static_cast<int>(normalizedWeights.size()); ++index) {
        adjustedAllocationRows[index].weight = normalizedWeights[static_cast<std::size_t>(index)];
    }

    const QVariantMap adjustedExposures = buildEstimatedExposureState(adjustedAllocationRows, 100.0);
    const double nextValue = currentExposureForFocus(adjustedExposures, focusType, focusKey);

    double totalAdjustedWeight = 0.0;
    for (double weight : normalizedWeights) {
        totalAdjustedWeight += weight;
    }

    QVariantMap diagnostics;
    diagnostics.insert(QStringLiteral("focusType"), focusType);
    diagnostics.insert(QStringLiteral("focusKey"), focusKey);
    diagnostics.insert(QStringLiteral("adjustmentMode"), reduceFocus ? QStringLiteral("reduce") : QStringLiteral("increase"));
    diagnostics.insert(QStringLiteral("beforeValue"), currentValue);
    diagnostics.insert(QStringLiteral("targetValue"), targetValue);
    diagnostics.insert(QStringLiteral("afterValue"), nextValue);
    diagnostics.insert(QStringLiteral("minWeightPercent"), minWeightPercent);
    diagnostics.insert(QStringLiteral("maxWeightPercent"), maxWeightPercent);
    diagnostics.insert(QStringLiteral("tiltStrength"), tiltStrength);

    return QVariantMap{{QStringLiteral("success"), true},
                       {QStringLiteral("message"), exposureAdjustActionLabel(focusType, focusKey, reduceFocus)},
                       {QStringLiteral("allocations"), adjustedAllocations},
                       {QStringLiteral("totalWeight"), totalAdjustedWeight},
                       {QStringLiteral("exposuresBefore"), estimatedExposures},
                       {QStringLiteral("exposuresAfter"), adjustedExposures},
                       {QStringLiteral("diagnostics"), diagnostics}};
}

int stableBucketIndex(const QString& text, int bucketCount)
{
    if (bucketCount <= 0) {
        return 0;
    }

    int hash = 0;
    for (const QChar ch : text) {
        hash = (hash * 31 + ch.unicode()) % 2147483647;
    }
    return std::abs(hash) % bucketCount;
}

QVariantMap buildEstimatedMetrics(const QList<PortfolioFactorAllocation>& allocations)
{
    if (allocations.isEmpty()) {
        return defaultMetrics();
    }

    double totalIc = 0.0;
    double totalIr = 0.0;
    double totalTurnover = 0.0;
    double diversificationBonus = 0.0;

    for (const PortfolioFactorAllocation& allocation : allocations) {
        totalIc += allocation.icValue;
        totalIr += allocation.irValue;
        totalTurnover += allocation.turnoverRate;
        diversificationBonus += (1.0 - std::fabs(allocation.correlation)) * 1.5;
    }

    const double factorCount = static_cast<double>(allocations.size());
    const double averageIc = totalIc / factorCount;
    const double averageIr = totalIr / factorCount;
    const double averageTurnover = totalTurnover / factorCount;

    QVariantMap result;
    result.insert(QStringLiteral("annualReturn"), clampValue(8.0 + averageIc * 120.0 + averageIr * 2.2 + diversificationBonus, 0.0, 50.0));
    result.insert(QStringLiteral("sharpeRatio"), clampValue(0.6 + averageIr * 0.45 + diversificationBonus * 0.08, 0.0, 4.0));
    result.insert(QStringLiteral("maxDrawdown"), clampValue(18.0 - diversificationBonus - (std::min)(averageIc * 20.0, 4.0) + averageTurnover * 0.03, 0.0, 50.0));
    return result;
}

QVariantMap resolveEffectiveMetrics(const QVariantMap& estimatedMetrics, const QVariantMap& latestBacktest)
{
    if (!hasObjectData(latestBacktest)) {
        QVariantMap result = estimatedMetrics;
        result.insert(QStringLiteral("source"), QStringLiteral("estimated"));
        result.insert(QStringLiteral("recordedAt"), QString());
        return result;
    }

    const QVariantMap summary = latestBacktest.value(QStringLiteral("summary")).toMap();
    QVariantMap result;
    result.insert(QStringLiteral("annualReturn"), normalizePercentMetric(
        summary.value(QStringLiteral("annualReturn"),
        summary.value(QStringLiteral("annualizedReturn"), summary.value(QStringLiteral("returns"))))));
    result.insert(QStringLiteral("sharpeRatio"), numberOrDefault(summary.value(QStringLiteral("sharpeRatio")), numberOrDefault(estimatedMetrics.value(QStringLiteral("sharpeRatio")), 0.0)));
    result.insert(QStringLiteral("maxDrawdown"), std::fabs(normalizePercentMetric(summary.value(QStringLiteral("maxDrawdown"), estimatedMetrics.value(QStringLiteral("maxDrawdown"))))));
    result.insert(QStringLiteral("source"), QStringLiteral("latestBacktest"));
    result.insert(QStringLiteral("recordedAt"), latestBacktest.value(QStringLiteral("recordedAt")).toString());
    return result;
}

QVariantMap buildEstimatedExposureState(const QList<PortfolioFactorAllocation>& allocations, double totalWeight)
{
    const QStringList sectors = {QStringLiteral("银行"), QStringLiteral("消费"), QStringLiteral("医药"), QStringLiteral("科技")};
    QVariantMap sectorWeights = emptySectorExposure();

    QVariantMap styleWeights;
    styleWeights.insert(QStringLiteral("市值"), 0.3);
    styleWeights.insert(QStringLiteral("动量"), 0.3);
    styleWeights.insert(QStringLiteral("价值"), 0.3);
    styleWeights.insert(QStringLiteral("波动率"), 0.3);

    if (allocations.isEmpty() || totalWeight <= 0.0) {
        QVariantMap result;
        result.insert(QStringLiteral("sector"), sectorWeights);
        result.insert(QStringLiteral("style"), styleWeights);
        return result;
    }

    for (const PortfolioFactorAllocation& allocation : allocations) {
        const double weightRatio = allocation.weight / (std::max)(totalWeight, 1.0);
        const int bucket = stableBucketIndex(allocation.factorId.isEmpty() ? allocation.displayName : allocation.factorId, sectors.size());
        const int secondaryBucket = (bucket + 1) % sectors.size();
        const double correlation = std::fabs(allocation.correlation);

        sectorWeights.insert(sectors[bucket], numberOrDefault(sectorWeights.value(sectors[bucket]), 0.0) + weightRatio * 0.65);
        sectorWeights.insert(sectors[secondaryBucket], numberOrDefault(sectorWeights.value(sectors[secondaryBucket]), 0.0) + weightRatio * 0.35);

        if (allocation.category == QStringLiteral("动量类")) {
            styleWeights.insert(QStringLiteral("动量"), numberOrDefault(styleWeights.value(QStringLiteral("动量")), 0.0) + weightRatio * 1.6);
            styleWeights.insert(QStringLiteral("波动率"), numberOrDefault(styleWeights.value(QStringLiteral("波动率")), 0.0) + correlation * weightRatio * 0.5);
        } else if (allocation.category == QStringLiteral("价值类")) {
            styleWeights.insert(QStringLiteral("价值"), numberOrDefault(styleWeights.value(QStringLiteral("价值")), 0.0) + weightRatio * 1.6);
            styleWeights.insert(QStringLiteral("市值"), numberOrDefault(styleWeights.value(QStringLiteral("市值")), 0.0) + weightRatio * 0.5);
        } else if (allocation.category == QStringLiteral("质量类")) {
            styleWeights.insert(QStringLiteral("市值"), numberOrDefault(styleWeights.value(QStringLiteral("市值")), 0.0) + weightRatio * 0.9);
            styleWeights.insert(QStringLiteral("价值"), numberOrDefault(styleWeights.value(QStringLiteral("价值")), 0.0) + weightRatio * 0.6);
        } else if (allocation.category == QStringLiteral("情绪类")) {
            styleWeights.insert(QStringLiteral("动量"), numberOrDefault(styleWeights.value(QStringLiteral("动量")), 0.0) + weightRatio * 0.9);
            styleWeights.insert(QStringLiteral("波动率"), numberOrDefault(styleWeights.value(QStringLiteral("波动率")), 0.0) + weightRatio * 0.9);
        } else if (allocation.category == QStringLiteral("流动性类")) {
            styleWeights.insert(QStringLiteral("市值"), numberOrDefault(styleWeights.value(QStringLiteral("市值")), 0.0) + weightRatio * 0.7);
            styleWeights.insert(QStringLiteral("波动率"), numberOrDefault(styleWeights.value(QStringLiteral("波动率")), 0.0) + weightRatio * 0.7);
        } else {
            styleWeights.insert(QStringLiteral("市值"), numberOrDefault(styleWeights.value(QStringLiteral("市值")), 0.0) + weightRatio * 0.4);
            styleWeights.insert(QStringLiteral("价值"), numberOrDefault(styleWeights.value(QStringLiteral("价值")), 0.0) + weightRatio * 0.4);
            styleWeights.insert(QStringLiteral("动量"), numberOrDefault(styleWeights.value(QStringLiteral("动量")), 0.0) + weightRatio * 0.4);
        }
    }

    for (auto it = sectorWeights.begin(); it != sectorWeights.end(); ++it) {
        it.value() = clampValue(it.value().toDouble(), 0.0, 1.0);
    }
    for (auto it = styleWeights.begin(); it != styleWeights.end(); ++it) {
        it.value() = clampValue(it.value().toDouble(), 0.0, 2.0);
    }

    QVariantMap result;
    result.insert(QStringLiteral("sector"), sectorWeights);
    result.insert(QStringLiteral("style"), styleWeights);
    return result;
}

double resolvePositionWeightRatio(const QVariantMap& position)
{
    const double ratioPercent = numberOrDefault(position.value(QStringLiteral("ratioValue")), 0.0);
    if (ratioPercent > 0.0) {
        return ratioPercent / 100.0;
    }

    QString ratioText = position.value(QStringLiteral("ratio")).toString();
    ratioText.remove(QLatin1Char('%'));
    bool ok = false;
    const double numericRatio = ratioText.toDouble(&ok);
    return ok ? numericRatio / 100.0 : 0.0;
}

QVariantMap normalizeSectorExposure(const QVariantMap& sectorWeights)
{
    QList<QPair<QString, double>> entries;
    for (auto it = sectorWeights.constBegin(); it != sectorWeights.constEnd(); ++it) {
        entries.push_back({it.key(), numberOrDefault(it.value(), 0.0)});
    }

    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        if (left.second == right.second) {
            return left.first < right.first;
        }
        return left.second > right.second;
    });

    QVariantMap normalized;
    for (int index = 0; index < entries.size() && index < 4; ++index) {
        if (entries[index].second <= 0.0) {
            continue;
        }
        normalized.insert(entries[index].first, clampValue(entries[index].second, 0.0, 1.0));
    }

    return normalized.isEmpty() ? emptySectorExposure() : normalized;
}

QVariantMap buildExposureState(const QVariantMap& snapshot, const QVariantMap& estimatedExposures)
{
    if (snapshot.value(QStringLiteral("status")).toString() != QStringLiteral("success")) {
        return estimatedExposures;
    }

    const QVariantList positions = snapshot.value(QStringLiteral("positions")).toList();
    if (positions.isEmpty()) {
        return estimatedExposures;
    }

    MarketDataService* marketDataService = MarketDataService::instance();
    if (!marketDataService) {
        return estimatedExposures;
    }

    QVariantMap sectorWeights;
    double weightedCapScore = 0.0;
    double resolvedCapWeight = 0.0;
    double totalSnapshotWeight = 0.0;

    for (const QVariant& positionVariant : positions) {
        const QVariantMap position = positionVariant.toMap();
        const QString symbol = position.value(QStringLiteral("symbol"), position.value(QStringLiteral("name"))).toString().trimmed();
        if (symbol.isEmpty()) {
            continue;
        }

        const double weightRatio = resolvePositionWeightRatio(position);
        if (weightRatio <= 0.0) {
            continue;
        }

        totalSnapshotWeight += weightRatio;

        const QVariantMap instrument = marketDataService->resolveInstrument(symbol);
        QString industryName = instrument.value(QStringLiteral("industry"),
            instrument.value(QStringLiteral("industryName"), instrument.value(QStringLiteral("sector")))).toString().trimmed();
        if (industryName.isEmpty()) {
            industryName = QStringLiteral("其他");
        }
        sectorWeights.insert(industryName, numberOrDefault(sectorWeights.value(industryName), 0.0) + weightRatio);

        const QVariant marketCapValue = instrument.contains(QStringLiteral("marketCap"))
            ? instrument.value(QStringLiteral("marketCap"))
            : (instrument.contains(QStringLiteral("market_cap"))
                ? instrument.value(QStringLiteral("market_cap"))
                : (instrument.contains(QStringLiteral("circulatingMarketCap"))
                    ? instrument.value(QStringLiteral("circulatingMarketCap"))
                    : instrument.value(QStringLiteral("circulating_market_cap"))));
        const double marketCap = numberOrDefault(marketCapValue, 0.0);
        if (marketCap > 0.0 && std::isfinite(marketCap)) {
            const double logCap = std::log(marketCap) / std::log(10.0);
            const double normalizedCap = clampValue((logCap - 8.5) / 3.5, 0.0, 1.0);
            weightedCapScore += normalizedCap * weightRatio;
            resolvedCapWeight += weightRatio;
        }
    }

    QVariantMap styleWeights = estimatedExposures.value(QStringLiteral("style")).toMap();
    if (resolvedCapWeight > 0.0) {
        const double averagedCapScore = weightedCapScore / resolvedCapWeight;
        styleWeights.insert(QStringLiteral("市值"), clampValue(0.4 + averagedCapScore * 1.4, 0.0, 2.0));
    }

    if (totalSnapshotWeight > 0.0) {
        double topSectorWeight = 0.0;
        for (auto it = sectorWeights.constBegin(); it != sectorWeights.constEnd(); ++it) {
            topSectorWeight = (std::max)(topSectorWeight, numberOrDefault(it.value(), 0.0));
        }
        styleWeights.insert(QStringLiteral("波动率"), clampValue((std::max)(numberOrDefault(styleWeights.value(QStringLiteral("波动率")), 0.0), 0.6 + topSectorWeight * 2.0), 0.0, 2.0));
    }

    QVariantMap result;
    result.insert(QStringLiteral("sector"), normalizeSectorExposure(sectorWeights));
    result.insert(QStringLiteral("style"), styleWeights.isEmpty() ? emptyStyleExposure() : styleWeights);
    return result;
}

QString normalizedPlanSymbol(const QVariantMap& row)
{
    return row.value(QStringLiteral("symbol"), row.value(QStringLiteral("name"))).toString().trimmed().toUpper();
}

double accountTotalAssetValue(const QVariantMap& accountSnapshot)
{
    const double totalAsset = numberOrDefault(
        accountSnapshot.value(QStringLiteral("totalAsset"), accountSnapshot.value(QStringLiteral("total_asset"))),
        0.0);
    if (totalAsset > 0.0) {
        return totalAsset;
    }

    const double availableCash = numberOrDefault(
        accountSnapshot.value(QStringLiteral("availableCash"), accountSnapshot.value(QStringLiteral("available_cash"))),
        0.0);
    const double marketValue = numberOrDefault(
        accountSnapshot.value(QStringLiteral("marketValue"), accountSnapshot.value(QStringLiteral("market_value"))),
        0.0);
    return availableCash + marketValue;
}

qint64 positionQuantity(const QVariantMap& position)
{
    return static_cast<qint64>(numberOrDefault(position.value(QStringLiteral("quantity")), 0.0));
}

qint64 closeablePositionQuantity(const QVariantMap& position)
{
    QVariant closeableValue = position.value(QStringLiteral("closeableQuantity"));
    if (!closeableValue.isValid()) {
        closeableValue = position.value(QStringLiteral("closeable_quantity"));
    }
    if (!closeableValue.isValid()) {
        closeableValue = position.value(QStringLiteral("availableQuantity"));
    }
    if (!closeableValue.isValid()) {
        closeableValue = position.value(QStringLiteral("available_quantity"));
    }
    if (!closeableValue.isValid()) {
        closeableValue = position.value(QStringLiteral("quantity"));
    }

    const double closeable = numberOrDefault(closeableValue, 0.0);
    return static_cast<qint64>(closeable);
}

qint64 normalizedTargetQuantity(double totalAsset, double weightRatio, double price)
{
    if (!(totalAsset > 0.0) || !(weightRatio > 0.0) || !(price > 0.0)) {
        return 0;
    }

    const double targetNotional = totalAsset * weightRatio;
    const double boardLots = std::floor(targetNotional / price / 100.0);
    const qint64 quantity = static_cast<qint64>(boardLots) * 100;
    return quantity > 0 ? quantity : 0;
}

QVariantMap buildExecutionOrderRow(const QString& symbol,
                                  const QString& side,
                                  qint64 quantity,
                                  double price,
                                  double targetWeightPercent,
                                  double currentWeightPercent,
                                  qint64 currentQuantity,
                                  qint64 targetQuantity,
                                  const QString& reason)
{
    QVariantMap order;
    order.insert(QStringLiteral("symbol"), symbol);
    order.insert(QStringLiteral("side"), side);
    order.insert(QStringLiteral("action"), side == QStringLiteral("SELL") ? QStringLiteral("sell") : QStringLiteral("buy"));
    order.insert(QStringLiteral("quantity"), quantity);
    order.insert(QStringLiteral("price"), price);
    order.insert(QStringLiteral("orderType"), QStringLiteral("LIMIT"));
    order.insert(QStringLiteral("mode"), QStringLiteral("stock"));
    order.insert(QStringLiteral("positionEffect"), side == QStringLiteral("SELL") ? QStringLiteral("CLOSE") : QStringLiteral("OPEN"));
    order.insert(QStringLiteral("requestedNotional"), price > 0.0 ? price * static_cast<double>(quantity) : 0.0);
    order.insert(QStringLiteral("targetWeightPercent"), targetWeightPercent);
    order.insert(QStringLiteral("currentWeightPercent"), currentWeightPercent);
    order.insert(QStringLiteral("currentQuantity"), currentQuantity);
    order.insert(QStringLiteral("targetQuantity"), targetQuantity);
    order.insert(QStringLiteral("deltaQuantity"), side == QStringLiteral("SELL") ? -quantity : quantity);
    order.insert(QStringLiteral("reason"), reason);
    return order;
}

QVariantMap buildSystemStatus(const QVariantMap& snapshot, const QVariantMap& latestBacktest, const QVariantMap& metrics)
{
    Q_UNUSED(metrics);

    int connectedServices = 0;
    if (FactorService::instance()) {
        connectedServices += 1;
    }
    if (StrategyService::instance()) {
        connectedServices += 1;
    }
    if (RiskMonitorService::instance()) {
        connectedServices += 1;
    }

    QString backtestStatus = QStringLiteral("🟡");
    QString backtestValue = QStringLiteral("待回测");
    QString backtestColor = QStringLiteral("#F59E0B");
    if (hasObjectData(latestBacktest)) {
        const QVariantMap summary = latestBacktest.value(QStringLiteral("summary")).toMap();
        backtestStatus = QStringLiteral("🟢");
        backtestValue = QString::number(normalizePercentMetric(summary.value(QStringLiteral("returns"))), 'f', 1) + QStringLiteral("%");
        backtestColor = numberOrDefault(summary.value(QStringLiteral("sharpeRatio")), 0.0) >= 1.0
            ? QStringLiteral("#10B981")
            : QStringLiteral("#F59E0B");
    }

    QString snapshotValue = QStringLiteral("待生成");
    QString snapshotColor = QStringLiteral("#F59E0B");
    QString snapshotStatus = QStringLiteral("🟡");
    const QString snapshotState = snapshot.value(QStringLiteral("status")).toString();
    if (snapshotState == QStringLiteral("success")) {
        snapshotValue = QString::number(snapshot.value(QStringLiteral("positions")).toList().size()) + QStringLiteral(" 只");
        snapshotColor = QStringLiteral("#10B981");
        snapshotStatus = QStringLiteral("🟢");
    } else if (snapshotState == QStringLiteral("error")) {
        snapshotValue = QStringLiteral("生成失败");
        snapshotColor = QStringLiteral("#EF4444");
        snapshotStatus = QStringLiteral("🔴");
    } else if (snapshotState == QStringLiteral("unavailable")) {
        snapshotValue = QStringLiteral("服务未连");
        snapshotColor = QStringLiteral("#EF4444");
        snapshotStatus = QStringLiteral("🔴");
    }

    QVariantMap result;
    result.insert(QStringLiteral("因子池"), QVariantMap{{QStringLiteral("status"), QStringLiteral("📚")}, {QStringLiteral("value"), QStringLiteral("已接入")}, {QStringLiteral("color"), QStringLiteral("#3B82F6")}});
    result.insert(QStringLiteral("当前组合"), QVariantMap{{QStringLiteral("status"), QStringLiteral("🧩")}, {QStringLiteral("value"), QStringLiteral("已构建")}, {QStringLiteral("color"), QStringLiteral("#10B981")}});
    result.insert(QStringLiteral("最近回测"), QVariantMap{{QStringLiteral("status"), backtestStatus}, {QStringLiteral("value"), backtestValue}, {QStringLiteral("color"), backtestColor}});
    result.insert(QStringLiteral("风险快照"), QVariantMap{{QStringLiteral("status"), snapshotStatus}, {QStringLiteral("value"), snapshotValue}, {QStringLiteral("color"), snapshotColor}});
    result.insert(QStringLiteral("数据源"), QVariantMap{{QStringLiteral("status"), connectedServices == 3 ? QStringLiteral("🟢") : QStringLiteral("🟡")}, {QStringLiteral("value"), QString::number(connectedServices) + QStringLiteral("/3 已连接")}, {QStringLiteral("color"), connectedServices == 3 ? QStringLiteral("#10B981") : QStringLiteral("#F59E0B")}});
    return result;
}

QString buildPortfolioSuggestion(int factorCount,
                                 double totalWeight,
                                 const QVariantMap& metrics,
                                 const QVariantMap& snapshot,
                                 const QVariantMap& exposures,
                                 const QVariantMap& latestBacktest)
{
    if (factorCount == 0) {
        return QStringLiteral("先从左侧添加真实因子，系统会自动生成组合快照。");
    }

    if (hasObjectData(latestBacktest)) {
        const QVariantMap summary = latestBacktest.value(QStringLiteral("summary")).toMap();
        const double latestDrawdown = std::fabs(normalizePercentMetric(summary.value(QStringLiteral("maxDrawdown"))));
        const double latestWinRate = normalizePercentMetric(summary.value(QStringLiteral("winRate")));
        if (latestDrawdown > 20.0) {
            return QStringLiteral("最近一次真实回测最大回撤较高，建议先降低高相关因子权重。");
        }
        if (latestWinRate > 0.0 && latestWinRate < 45.0) {
            return QStringLiteral("最近一次真实回测胜率偏低，建议复核调仓周期和止损参数。");
        }
    }

    if (std::fabs(totalWeight - 100.0) > 0.5) {
        return QStringLiteral("当前总权重偏离 100%，建议先重置权重后再保存或回测。");
    }

    const QString snapshotStatus = snapshot.value(QStringLiteral("status")).toString();
    if (snapshotStatus == QStringLiteral("error")) {
        return snapshot.value(QStringLiteral("error")).toString().trimmed().isEmpty()
            ? QStringLiteral("风险快照生成失败，建议先检查因子是否具备最新交易日数据。")
            : snapshot.value(QStringLiteral("error")).toString();
    }

    const QVariantList positions = snapshot.value(QStringLiteral("positions")).toList();
    if (snapshotStatus == QStringLiteral("success") && positions.size() < 5) {
        return QStringLiteral("当前快照候选持仓较少，建议补充低相关因子提升覆盖面。");
    }

    const QVariantMap style = exposures.value(QStringLiteral("style")).toMap();
    if (numberOrDefault(style.value(QStringLiteral("价值")), 0.0) < 0.5) {
        return QStringLiteral("价值暴露偏低，可以加入价值类因子平衡组合风格。");
    }

    if (numberOrDefault(metrics.value(QStringLiteral("maxDrawdown")), 0.0) > 20.0) {
        return QStringLiteral("预估回撤偏高，建议降低高相关因子权重或执行一键优化。");
    }

    return QStringLiteral("组合结构已具备最小闭环条件，可以继续保存并推送到回测。");
}

QVariantList buildAutomaticNotifications(int factorCount,
                                         double totalWeight,
                                         const QVariantMap& snapshot,
                                         const QVariantMap& metrics,
                                         const QVariantMap& latestBacktest)
{
    QVariantList items;

    if (factorCount == 0) {
        items.append(createNotification(QStringLiteral("info"), QStringLiteral("等待添加组合因子"), QStringLiteral("添加因子")));
        return items;
    }

    if (std::fabs(totalWeight - 100.0) > 0.5) {
        items.append(createNotification(
            QStringLiteral("warning"),
            QStringLiteral("总权重为 %1% ，建议重置到 100%").arg(QString::number(totalWeight, 'f', 1)),
            QStringLiteral("重置")));
    }

    const QString snapshotStatus = snapshot.value(QStringLiteral("status")).toString();
    if (snapshotStatus == QStringLiteral("success")) {
        const QString snapshotDate = snapshot.value(QStringLiteral("snapshotDate"), snapshot.value(QStringLiteral("recordedAt"), currentTimeLabel())).toString();
        items.append(createNotification(
            QStringLiteral("success"),
            QStringLiteral("已生成真实组合快照，候选持仓 %1 只，快照日 %2")
                .arg(QString::number(snapshot.value(QStringLiteral("positions")).toList().size()), snapshotDate),
            QStringLiteral("查看")));
    } else if (snapshotStatus == QStringLiteral("error")) {
        items.append(createNotification(
            QStringLiteral("warning"),
            snapshot.value(QStringLiteral("error")).toString().trimmed().isEmpty()
                ? QStringLiteral("组合快照生成失败")
                : snapshot.value(QStringLiteral("error")).toString(),
            QStringLiteral("检查")));
    } else if (snapshotStatus == QStringLiteral("unavailable")) {
        items.append(createNotification(QStringLiteral("warning"), QStringLiteral("风险监控服务未连接，当前仅展示估算指标"), QStringLiteral("检查")));
    }

    if (hasObjectData(latestBacktest)) {
        const QVariantMap summary = latestBacktest.value(QStringLiteral("summary")).toMap();
        items.append(createNotification(
            QStringLiteral("success"),
            QStringLiteral("最近回测收益 %1% ，夏普 %2")
                .arg(QString::number(normalizePercentMetric(summary.value(QStringLiteral("returns"))), 'f', 1),
                     QString::number(numberOrDefault(summary.value(QStringLiteral("sharpeRatio")), 0.0), 'f', 2)),
            QStringLiteral("查看")));
    } else {
        items.append(createNotification(
            QStringLiteral("info"),
            QStringLiteral("预估年化收益 %1% ，可直接保存为策略")
                .arg(QString::number(numberOrDefault(metrics.value(QStringLiteral("annualReturn")), 0.0), 'f', 1)),
            QStringLiteral("保存")));
    }

    return items;
}

QVariantMap unavailablePortfolioState(const QString& reason)
{
    QVariantMap result;
    result.insert(QStringLiteral("metrics"), defaultMetrics());
    result.insert(QStringLiteral("exposures"), QVariantMap{{QStringLiteral("sector"), emptySectorExposure()}, {QStringLiteral("style"), emptyStyleExposure()}});
    result.insert(QStringLiteral("snapshot"), defaultSnapshot(QStringLiteral("unavailable"), reason));
    result.insert(QStringLiteral("backtest"), QVariantMap{});
    result.insert(QStringLiteral("systemStatus"), QVariantMap{});
    result.insert(QStringLiteral("notifications"), QVariantList{createNotification(QStringLiteral("warning"), reason, QStringLiteral("检查"))});
    result.insert(QStringLiteral("insights"), QVariantMap{{QStringLiteral("suggestion"), reason}});
    result.insert(QStringLiteral("lastUpdated"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    return result;
}

}  // namespace

PortfolioAnalysisService* PortfolioAnalysisService::m_instance = nullptr;
QMutex PortfolioAnalysisService::m_instanceMutex;

PortfolioAnalysisService* PortfolioAnalysisService::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new PortfolioAnalysisService();
        m_instance->initialize();
    }
    return m_instance;
}

PortfolioAnalysisService::PortfolioAnalysisService(QObject* parent)
    : QObject(parent)
    , m_initialized(false)
{
}

void PortfolioAnalysisService::initialize()
{
    QMutexLocker locker(&m_mutex);
    if (m_initialized) {
        return;
    }

    FactorService::instance();
    StrategyService::instance();
    MarketDataService::instance();
    RiskMonitorService::instance();

    m_initialized = true;
    emit initializedChanged();
}

bool PortfolioAnalysisService::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}

QVariantMap PortfolioAnalysisService::analyzePortfolioState(const QVariantMap& strategy,
                                                            const QVariantMap& latestBacktest)
{
    if (strategy.isEmpty()) {
        return unavailablePortfolioState(QStringLiteral("缺少组合上下文，无法分析组合状态"));
    }

    const QVariantMap resolvedStrategyConfig = buildResolvedStrategyConfig(strategy, QVariantMap{});
    const QList<PortfolioFactorAllocation> allocations = parseAllocations(strategy, resolvedStrategyConfig);
    const double totalWeight = totalAllocationWeight(allocations);

    const QVariantMap estimatedMetrics = buildEstimatedMetrics(allocations);
    const QVariantMap metrics = resolveEffectiveMetrics(estimatedMetrics, latestBacktest);
    const QVariantMap estimatedExposures = buildEstimatedExposureState(allocations, totalWeight);

    QVariantMap snapshot;
    if (allocations.isEmpty()) {
        snapshot = defaultSnapshot(QStringLiteral("idle"), QStringLiteral("当前组合为空"));
    } else {
        RiskMonitorService* riskMonitorService = RiskMonitorService::instance();
        if (!riskMonitorService) {
            snapshot = defaultSnapshot(QStringLiteral("unavailable"), QStringLiteral("RiskMonitorService 未连接"));
        } else {
            snapshot = riskMonitorService->buildPortfolioSnapshot(strategy, latestBacktest);
            if (!snapshot.contains(QStringLiteral("status"))) {
                snapshot.insert(QStringLiteral("status"), QStringLiteral("error"));
            }
            if (!snapshot.contains(QStringLiteral("positions"))) {
                snapshot.insert(QStringLiteral("positions"), QVariantList{});
            }
            if (!snapshot.contains(QStringLiteral("diagnostics"))) {
                snapshot.insert(QStringLiteral("diagnostics"), QVariantMap{});
            }
        }
    }

    const QVariantMap exposures = buildExposureState(snapshot, estimatedExposures);
    const QVariantList notifications = buildAutomaticNotifications(allocations.size(), totalWeight, snapshot, metrics, latestBacktest);

    QVariantMap result;
    result.insert(QStringLiteral("metrics"), metrics);
    result.insert(QStringLiteral("exposures"), exposures);
    result.insert(QStringLiteral("snapshot"), snapshot);
    result.insert(QStringLiteral("backtest"), latestBacktest);
    result.insert(QStringLiteral("systemStatus"), buildSystemStatus(snapshot, latestBacktest, metrics));
    result.insert(QStringLiteral("notifications"), notifications);
    result.insert(QStringLiteral("insights"), QVariantMap{{QStringLiteral("suggestion"), buildPortfolioSuggestion(allocations.size(), totalWeight, metrics, snapshot, exposures, latestBacktest)}});
    result.insert(QStringLiteral("lastUpdated"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    return result;
}

QVariantMap PortfolioAnalysisService::buildPortfolioExecutionPlan(const QVariantMap& strategy,
                                                                 const QVariantMap& latestBacktest)
{
    if (strategy.isEmpty()) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), QStringLiteral("缺少组合上下文，无法生成调仓计划")}};
    }

    RiskMonitorService* riskMonitorService = RiskMonitorService::instance();
    if (!riskMonitorService) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), QStringLiteral("RiskMonitorService 未连接，无法生成调仓计划")}};
    }

    PositionAccountService* positionAccountService = PositionAccountService::instance();
    if (!positionAccountService) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), QStringLiteral("PositionAccountService 未连接，无法生成调仓计划")}};
    }

    positionAccountService->initialize();
    if (!positionAccountService->initialSnapshotLoaded()) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), QStringLiteral("持仓快照尚未同步完成，无法生成调仓计划")}};
    }

    const QVariantMap resolvedStrategyConfig = buildResolvedStrategyConfig(strategy, QVariantMap{});
    const QVariantMap batchOptions = resolveExecutionBatchOptions(resolvedStrategyConfig);

    const QVariantMap snapshot = riskMonitorService->buildPortfolioSnapshot(strategy, latestBacktest);
    if (snapshot.value(QStringLiteral("status")).toString() != QStringLiteral("success")) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), snapshot.value(QStringLiteral("error")).toString().trimmed().isEmpty()
                               ? QStringLiteral("目标持仓快照生成失败")
                               : snapshot.value(QStringLiteral("error")).toString()},
                           {QStringLiteral("snapshot"), snapshot}};
    }

    const QVariantMap accountSnapshot = positionAccountService->accountSnapshot();
    const QVariantList currentPositions = positionAccountService->positions();
    const QVariantList targetPositions = snapshot.value(QStringLiteral("positions")).toList();
    const double totalAsset = accountTotalAssetValue(accountSnapshot);

    if (!(totalAsset > 0.0)) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), QStringLiteral("账户总资产无效，无法按目标权重换算调仓股数")},
                           {QStringLiteral("snapshot"), snapshot},
                           {QStringLiteral("accountSnapshot"), accountSnapshot}};
    }

    QHash<QString, QVariantMap> currentBySymbol;
    for (const QVariant& positionVariant : currentPositions) {
        const QVariantMap position = positionVariant.toMap();
        const QString symbol = normalizedPlanSymbol(position);
        if (!symbol.isEmpty()) {
            currentBySymbol.insert(symbol, position);
        }
    }

    QSet<QString> targetSymbols;
    QVariantList orders;
    QVariantList skippedOrders;
    double estimatedBuyNotional = 0.0;
    double estimatedSellNotional = 0.0;
    int buyOrderCount = 0;
    int sellOrderCount = 0;

    for (const QVariant& positionVariant : targetPositions) {
        const QVariantMap targetPosition = positionVariant.toMap();
        const QString symbol = normalizedPlanSymbol(targetPosition);
        if (symbol.isEmpty()) {
            continue;
        }

        targetSymbols.insert(symbol);

        const double lastPrice = numberOrDefault(targetPosition.value(QStringLiteral("lastPrice")), 0.0);
        const double targetWeightRatio = resolvePositionWeightRatio(targetPosition);
        const double targetWeightPercent = targetWeightRatio * 100.0;
        if (!(lastPrice > 0.0)) {
            skippedOrders.append(QVariantMap{{QStringLiteral("symbol"), symbol},
                                             {QStringLiteral("reason"), QStringLiteral("缺少有效价格，跳过该目标持仓")}});
            continue;
        }

        const qint64 targetQuantity = normalizedTargetQuantity(totalAsset, targetWeightRatio, lastPrice);
        const QVariantMap currentPosition = currentBySymbol.value(symbol);
        const qint64 currentQuantity = positionQuantity(currentPosition);
        const double currentWeightPercent = totalAsset > 0.0
            ? (numberOrDefault(currentPosition.value(QStringLiteral("marketValue")), 0.0) / totalAsset) * 100.0
            : 0.0;

        if (targetQuantity > currentQuantity) {
            const qint64 quantity = targetQuantity - currentQuantity;
            if (quantity < 100) {
                skippedOrders.append(QVariantMap{{QStringLiteral("symbol"), symbol},
                                                 {QStringLiteral("reason"), QStringLiteral("目标增仓不足一手，跳过")},
                                                 {QStringLiteral("targetQuantity"), targetQuantity},
                                                 {QStringLiteral("currentQuantity"), currentQuantity}});
                continue;
            }

            const QVariantMap order = buildExecutionOrderRow(symbol,
                                                             QStringLiteral("BUY"),
                                                             quantity,
                                                             lastPrice,
                                                             targetWeightPercent,
                                                             currentWeightPercent,
                                                             currentQuantity,
                                                             targetQuantity,
                                                             QStringLiteral("按组合目标权重补足持仓"));
            estimatedBuyNotional += order.value(QStringLiteral("requestedNotional")).toDouble();
            buyOrderCount += 1;
            orders.append(order);
        } else if (currentQuantity > targetQuantity) {
            const qint64 closeableQuantity = closeablePositionQuantity(currentPosition);
            qint64 quantity = currentQuantity - targetQuantity;
            if (closeableQuantity <= 0) {
                skippedOrders.append(QVariantMap{{QStringLiteral("symbol"), symbol},
                                                 {QStringLiteral("reason"), QStringLiteral("无可卖数量，跳过减仓")},
                                                 {QStringLiteral("targetQuantity"), targetQuantity},
                                                 {QStringLiteral("currentQuantity"), currentQuantity}});
                continue;
            }

            if (quantity > closeableQuantity) {
                quantity = closeableQuantity;
            }
            if (quantity <= 0) {
                continue;
            }

            const QVariantMap order = buildExecutionOrderRow(symbol,
                                                             QStringLiteral("SELL"),
                                                             quantity,
                                                             lastPrice,
                                                             targetWeightPercent,
                                                             currentWeightPercent,
                                                             currentQuantity,
                                                             targetQuantity,
                                                             targetQuantity > 0
                                                                 ? QStringLiteral("按组合目标权重回收超配仓位")
                                                                 : QStringLiteral("目标组合不再持有该标的，执行清仓"));
            estimatedSellNotional += order.value(QStringLiteral("requestedNotional")).toDouble();
            sellOrderCount += 1;
            orders.append(order);
        }
    }

    for (auto it = currentBySymbol.constBegin(); it != currentBySymbol.constEnd(); ++it) {
        if (targetSymbols.contains(it.key())) {
            continue;
        }

        const qint64 closeableQuantity = closeablePositionQuantity(it.value());
        const double lastPrice = numberOrDefault(it.value().value(QStringLiteral("lastPrice")), 0.0);
        const qint64 currentQuantity = positionQuantity(it.value());
        if (closeableQuantity <= 0 || !(lastPrice > 0.0)) {
            skippedOrders.append(QVariantMap{{QStringLiteral("symbol"), it.key()},
                                             {QStringLiteral("reason"), QStringLiteral("目标组合外持仓缺少可卖数量或价格，跳过清仓")}});
            continue;
        }

        const QVariantMap order = buildExecutionOrderRow(it.key(),
                                                         QStringLiteral("SELL"),
                                                         closeableQuantity,
                                                         lastPrice,
                                                         0.0,
                                                         totalAsset > 0.0
                                                             ? (numberOrDefault(it.value().value(QStringLiteral("marketValue")), 0.0) / totalAsset) * 100.0
                                                             : 0.0,
                                                         currentQuantity,
                                                         0,
                                                         QStringLiteral("当前持仓不在目标组合内，执行退出"));
        estimatedSellNotional += order.value(QStringLiteral("requestedNotional")).toDouble();
        sellOrderCount += 1;
        orders.append(order);
    }

    const QVariantMap batchPlan = portfolio_execution_plan::buildSellFirstExecutionBatches(&orders, batchOptions);
    const QVariantList batches = batchPlan.value(QStringLiteral("batches")).toList();
    const QVariantMap batchSummary = batchPlan.value(QStringLiteral("summary")).toMap();

    QVariantMap summary;
    summary.insert(QStringLiteral("targetSymbolCount"), targetPositions.size());
    summary.insert(QStringLiteral("currentSymbolCount"), currentPositions.size());
    summary.insert(QStringLiteral("orderCount"), orders.size());
    summary.insert(QStringLiteral("buyOrderCount"), buyOrderCount);
    summary.insert(QStringLiteral("sellOrderCount"), sellOrderCount);
    summary.insert(QStringLiteral("sellFirstApplied"), true);
    summary.insert(QStringLiteral("batchCount"), batchSummary.value(QStringLiteral("batchCount")).toInt());
    summary.insert(QStringLiteral("sellBatchCount"), batchSummary.value(QStringLiteral("sellBatchCount")).toInt());
    summary.insert(QStringLiteral("buyBatchCount"), batchSummary.value(QStringLiteral("buyBatchCount")).toInt());
    summary.insert(QStringLiteral("requiresSequentialExecution"), batchSummary.value(QStringLiteral("requiresSequentialExecution")).toBool());
    summary.insert(QStringLiteral("batchMode"), batchSummary.value(QStringLiteral("batchMode")).toString());
    summary.insert(QStringLiteral("constrainedBatching"), batchSummary.value(QStringLiteral("constrainedBatching")).toBool());
    summary.insert(QStringLiteral("maxBatchOrders"), batchSummary.value(QStringLiteral("maxBatchOrders")).toInt());
    summary.insert(QStringLiteral("maxBatchNotionalWan"), batchSummary.value(QStringLiteral("maxBatchNotionalWan")).toDouble());
    summary.insert(QStringLiteral("estimatedBuyNotional"), estimatedBuyNotional);
    summary.insert(QStringLiteral("estimatedSellNotional"), estimatedSellNotional);
    summary.insert(QStringLiteral("totalAsset"), totalAsset);

    return QVariantMap{{QStringLiteral("success"), true},
                       {QStringLiteral("message"), orders.isEmpty()
                           ? QStringLiteral("当前持仓已接近目标组合，无需生成调仓委托")
                           : batchSummary.value(QStringLiteral("constrainedBatching")).toBool()
                               ? QStringLiteral("已按卖出优先和批次限额生成 %1 个执行批次，共 %2 笔调仓委托预览")
                                   .arg(batches.size())
                                   .arg(orders.size())
                               : QStringLiteral("已按卖出优先生成 %1 个执行批次，共 %2 笔调仓委托预览")
                               .arg(batches.size())
                               .arg(orders.size())},
                       {QStringLiteral("snapshot"), snapshot},
                       {QStringLiteral("accountSnapshot"), accountSnapshot},
                       {QStringLiteral("orders"), orders},
                       {QStringLiteral("batches"), batches},
                       {QStringLiteral("skippedOrders"), skippedOrders},
                       {QStringLiteral("summary"), summary},
                       {QStringLiteral("generatedAt"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))}};
}

QVariantMap PortfolioAnalysisService::optimizePortfolioAllocations(const QVariantMap& strategy,
                                                                   const QVariantMap& options)
{
    if (strategy.isEmpty()) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), QStringLiteral("缺少组合上下文，无法优化权重")}};
    }

    const QVariantMap resolvedStrategyConfig = buildResolvedStrategyConfig(strategy, options);
    const QList<PortfolioFactorAllocation> allocations = parseAllocations(strategy, resolvedStrategyConfig);
    if (allocations.isEmpty()) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), QStringLiteral("当前组合为空，无法优化")}};
    }

    const QString positionSizingMethod = resolvePositionSizingMethod(strategy, resolvedStrategyConfig);
    const double maxWeightPercent = resolveMaxWeightPercent(strategy, resolvedStrategyConfig, allocations.size());
    const double minWeightPercent = resolveMinWeightPercent(strategy, resolvedStrategyConfig, allocations.size());

    QHash<QString, int> categoryCounts;
    for (const PortfolioFactorAllocation& allocation : allocations) {
        categoryCounts[allocation.category] += 1;
    }

    std::vector<double> rawScores;
    rawScores.reserve(static_cast<std::size_t>(allocations.size()));
    double totalScore = 0.0;
    for (const PortfolioFactorAllocation& allocation : allocations) {
        const double score = (std::max)(0.01, scoreAllocationForMethod(allocation, positionSizingMethod, categoryCounts.value(allocation.category, 1)));
        rawScores.push_back(score);
        totalScore += score;
    }

    std::vector<double> rawWeights;
    rawWeights.reserve(rawScores.size());
    for (double score : rawScores) {
        rawWeights.push_back(totalScore > 0.0 ? (score / totalScore) * 100.0 : 0.0);
    }

    const std::vector<double> optimizedWeights = normalizeWeightsWithBounds(rawWeights, minWeightPercent, maxWeightPercent);
    QVariantList optimizedAllocations = buildOptimizedAllocationList(allocations, optimizedWeights);

    double optimizedTotal = 0.0;
    for (double weight : optimizedWeights) {
        optimizedTotal += weight;
    }

    QString methodLabel = QStringLiteral("评分约束");
    if (positionSizingMethod == QStringLiteral("kelly")) {
        methodLabel = QStringLiteral("凯利近似");
    } else if (positionSizingMethod == QStringLiteral("riskparity") || positionSizingMethod == QStringLiteral("risk_parity")) {
        methodLabel = QStringLiteral("风险平价近似");
    } else if (positionSizingMethod == QStringLiteral("equalweight") || positionSizingMethod == QStringLiteral("equal_weight")) {
        methodLabel = QStringLiteral("等权重");
    }

    QVariantMap diagnostics;
    diagnostics.insert(QStringLiteral("positionSizingMethod"), positionSizingMethod);
    diagnostics.insert(QStringLiteral("maxWeightPercent"), maxWeightPercent);
    diagnostics.insert(QStringLiteral("minWeightPercent"), minWeightPercent);
    diagnostics.insert(QStringLiteral("factorCount"), allocations.size());
    diagnostics.insert(QStringLiteral("optimizedTotalWeight"), optimizedTotal);

    return QVariantMap{{QStringLiteral("success"), true},
                       {QStringLiteral("message"), QStringLiteral("已按%1优化组合权重").arg(methodLabel)},
                       {QStringLiteral("allocations"), optimizedAllocations},
                       {QStringLiteral("totalWeight"), optimizedTotal},
                       {QStringLiteral("diagnostics"), diagnostics}};
}

QVariantMap PortfolioAnalysisService::adjustPortfolioExposure(const QVariantMap& strategy,
                                                              const QString& focusType,
                                                              const QString& focusKey,
                                                              const QVariantMap& options)
{
    if (strategy.isEmpty()) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), QStringLiteral("缺少组合上下文，无法调整暴露")}};
    }

    const QString normalizedFocusType = focusType.trimmed().toLower();
    const QString normalizedFocusKey = focusKey.trimmed();
    if (normalizedFocusType != QStringLiteral("sector") && normalizedFocusType != QStringLiteral("style")) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), QStringLiteral("未知暴露类型，无法调整")}};
    }
    if (normalizedFocusKey.isEmpty()) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), QStringLiteral("缺少目标项，无法调整暴露")}};
    }

    const QVariantMap resolvedStrategyConfig = buildResolvedStrategyConfig(strategy, options);
    const QList<PortfolioFactorAllocation> allocations = parseAllocations(strategy, resolvedStrategyConfig);
    return buildExposureAdjustmentResult(allocations, normalizedFocusType, normalizedFocusKey, strategy, options);
}

QVariantMap PortfolioAnalysisService::checkPortfolioRisk(const QVariantMap& strategy,
                                                         const QVariantMap& latestBacktest)
{
    const QVariantMap analyzedState = analyzePortfolioState(strategy, latestBacktest);
    const QVariantList notifications = analyzedState.value(QStringLiteral("notifications")).toList();
    const QVariantMap snapshot = analyzedState.value(QStringLiteral("snapshot")).toMap();
    const QVariantMap metrics = analyzedState.value(QStringLiteral("metrics")).toMap();

    int warningCount = 0;
    int infoCount = 0;
    int successCount = 0;
    for (const QVariant& item : notifications) {
        const QString type = item.toMap().value(QStringLiteral("type")).toString().trimmed().toLower();
        if (type == QStringLiteral("warning")) {
            warningCount += 1;
        } else if (type == QStringLiteral("success")) {
            successCount += 1;
        } else if (type == QStringLiteral("info")) {
            infoCount += 1;
        }
    }

    int dangerPositions = 0;
    int warningPositions = 0;
    const QVariantList positions = snapshot.value(QStringLiteral("positions")).toList();
    for (const QVariant& positionVariant : positions) {
        const QString badgeType = positionVariant.toMap().value(QStringLiteral("badgeType")).toString().trimmed().toLower();
        if (badgeType == QStringLiteral("danger")) {
            dangerPositions += 1;
        } else if (badgeType == QStringLiteral("warning")) {
            warningPositions += 1;
        }
    }

    QString message = QStringLiteral("组合风险检查完成，未发现明显异常");
    if (dangerPositions > 0) {
        message = QStringLiteral("组合风险检查完成，发现 %1 项高风险持仓").arg(dangerPositions);
    } else if (warningCount > 0 || warningPositions > 0) {
        message = QStringLiteral("组合风险检查完成，发现 %1 项预警").arg((std::max)(warningCount, warningPositions));
    } else if (snapshot.value(QStringLiteral("status")).toString() == QStringLiteral("unavailable")) {
        message = QStringLiteral("组合风险检查完成，但风险快照服务当前不可用");
    }

    QVariantMap diagnostics;
    diagnostics.insert(QStringLiteral("warningCount"), warningCount);
    diagnostics.insert(QStringLiteral("infoCount"), infoCount);
    diagnostics.insert(QStringLiteral("successCount"), successCount);
    diagnostics.insert(QStringLiteral("warningPositions"), warningPositions);
    diagnostics.insert(QStringLiteral("dangerPositions"), dangerPositions);
    diagnostics.insert(QStringLiteral("snapshotStatus"), snapshot.value(QStringLiteral("status")));
    diagnostics.insert(QStringLiteral("maxDrawdown"), metrics.value(QStringLiteral("maxDrawdown")));

    QVariantMap result = analyzedState;
    result.insert(QStringLiteral("success"), true);
    result.insert(QStringLiteral("message"), message);
    result.insert(QStringLiteral("diagnostics"), diagnostics);
    return result;
}