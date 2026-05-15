#include "FactorBacktestController.h"
#include "DataServiceCache.h"
#include "FactorBacktestPreflightUtils.h"
#include "FactorService.h"
#include "DataFetchFieldContractUtils.h"
#include "RiskConfigService.h"

#include <QDate>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace {

QString removedReason()
{
    return QStringLiteral("因子引擎侧业务代码已删除");
}

QVariantMap removedSupportInfo(const QString& factorId)
{
    QVariantMap info;
    info[QStringLiteral("factorId")] = factorId.trimmed();
    info[QStringLiteral("supported")] = false;
    info[QStringLiteral("category")] = QStringLiteral("engine-factor-removed");
    info[QStringLiteral("reason")] = removedReason();
    info[QStringLiteral("requiredFields")] = QVariantList{};
    info[QStringLiteral("missingFields")] = QVariantList{};
    return info;
}

QVariantMap removedFailure(const QString& factorId)
{
    QVariantMap failure;
    failure[QStringLiteral("factorId")] = factorId.trimmed();
    failure[QStringLiteral("instanceId")] = QString();
    failure[QStringLiteral("reason")] = removedReason();
    failure[QStringLiteral("category")] = QStringLiteral("engine-factor-removed");
    return failure;
}

QVariantList dedupeFactorIds(const QVariantList& factorIds)
{
    QVariantList normalized;
    QSet<QString> seen;
    for (const QVariant& factorIdValue : factorIds) {
        const QString factorId = factorIdValue.toString().trimmed();
        if (factorId.isEmpty() || seen.contains(factorId)) {
            continue;
        }
        seen.insert(factorId);
        normalized.append(factorId);
    }
    return normalized;
}

QString normalizedDataSourceMode(const QString& dataSourceMode)
{
    const QString normalized = dataSourceMode.trimmed().toLower();
    return normalized.isEmpty() ? QStringLiteral("cache") : normalized;
}

QVariantList toVariantList(const QStringList& values)
{
    QVariantList result;
    result.reserve(values.size());
    for (const QString& value : values) {
        result.append(value);
    }
    return result;
}

QStringList dedupeStringList(const QStringList& values)
{
    QStringList result;
    QSet<QString> seen;
    for (const QString& value : values) {
        const QString normalized = value.trimmed();
        if (normalized.isEmpty() || seen.contains(normalized)) {
            continue;
        }
        seen.insert(normalized);
        result.append(normalized);
    }
    return result;
}

QDate parseSupportDate(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return {};
    }
    if (value.canConvert<QDate>()) {
        const QDate date = value.toDate();
        if (date.isValid()) {
            return date;
        }
    }

    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return {};
    }

    const QDate isoDate = QDate::fromString(text, Qt::ISODate);
    if (isoDate.isValid()) {
        return isoDate;
    }

    const QDate dashedDate = QDate::fromString(text, QStringLiteral("yyyy-MM-dd"));
    if (dashedDate.isValid()) {
        return dashedDate;
    }

    return QDate::fromString(text, QStringLiteral("yyyyMMdd"));
}

int uniqueTradeDateCount(const QVariantList& rows)
{
    QSet<QDate> tradeDates;
    for (const QVariant& rowValue : rows) {
        const QVariantMap row = rowValue.toMap();
        const QDate tradeDate = parseSupportDate(
            row.value(factor::bridge::CommonFieldKeys::TRADE_DATE,
                      row.value(factor::bridge::LegacyCleaningFieldKeys::DATE)));
        if (tradeDate.isValid()) {
            tradeDates.insert(tradeDate);
        }
    }
    return tradeDates.size();
}

QSet<QString> collectAvailableFields(const QVariantMap& cacheSnapshot,
                                     const DataServiceCache::DataSetInfo& dataSetInfo,
                                     const QVariantList& rows)
{
    QSet<QString> fields;
    const auto appendFields = [&fields](const QStringList& values) {
        for (const QString& value : values) {
            const QString normalized = value.trimmed();
            if (!normalized.isEmpty()) {
                fields.insert(normalized);
            }
        }
    };

    appendFields(cacheSnapshot.value(QStringLiteral("availableFields")).toStringList());
    appendFields(dataSetInfo.availableFields);

    for (const QVariant& rowValue : rows) {
        const QVariantMap row = rowValue.toMap();
        for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
            const QString key = it.key().trimmed();
            const QString canonicalKey = factor::bridge::canonicalContractFieldName(key);
            if (key.isEmpty()
                || key == factor::bridge::CommonFieldKeys::SYMBOL
                || key == factor::bridge::CommonFieldKeys::TRADE_DATE
                || key == factor::bridge::LegacyCleaningFieldKeys::DATE) {
                continue;
            }
            fields.insert(canonicalKey.isEmpty() ? key : canonicalKey);
        }
    }

    return fields;
}

factor::FactorType resolveRuntimeType(const factor::FactorInstanceInfo& info,
                                      const std::shared_ptr<factor::BaseFactor>& factorInstance)
{
    if (factorInstance) {
        const factor::FactorType fromInstance = factorInstance->getFactorType();
        if (fromInstance != factor::FactorType::UNKNOWN) {
            return fromInstance;
        }
    }

    if (info.factorType != factor::FactorType::UNKNOWN) {
        return info.factorType;
    }

    return factor::FactorType::UNKNOWN;
}

bool configHasCustomExpression(const factor::FactorInstanceInfo& info)
{
    if (!info.config.has("calculation")) {
        return false;
    }
    const auto calculation = info.config.get("calculation");
    if (!calculation.has("expression")) {
        return false;
    }
    const auto expression = calculation.get("expression");
    return expression.isString() && !QString::fromStdString(expression.asString()).trimmed().isEmpty();
}

QStringList normalizedRequiredFields(factor::FactorType runtimeType,
                                     const factor::DataRequirements& requirements)
{
    QStringList fields;
    for (const std::string& field : requirements.requiredFields) {
        const QString normalized = QString::fromStdString(field).trimmed();
        if (!normalized.isEmpty()) {
            fields.append(normalized);
        }
    }

    if (runtimeType == factor::FactorType::DIVIDEND) {
        const QStringList orderedSpecialFields{
            factor::bridge::MarketBarFieldKeys::PRE_ADJ_FACTOR,
            factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR,
            factor::bridge::MarketBarFieldKeys::INDUSTRY_CODE,
            factor::bridge::MarketBarFieldKeys::MARKET_CAP};
        QStringList reordered;
        for (const QString& field : fields) {
            if (!orderedSpecialFields.contains(field)) {
                reordered.append(field);
            }
        }
        for (const QString& field : orderedSpecialFields) {
            if (fields.contains(field)) {
                reordered.append(field);
            }
        }
        fields = reordered;
    }

    return dedupeStringList(fields);
}

QVariantList normalizedStockPoolSymbols(const QVariantList& stockPoolSymbols)
{
    QVariantList normalized;
    QSet<QString> seen;
    for (const QVariant& value : stockPoolSymbols) {
        const QString symbol = value.toString().trimmed().toUpper();
        if (symbol.isEmpty() || seen.contains(symbol)) {
            continue;
        }
        seen.insert(symbol);
        normalized.append(symbol);
    }
    return normalized;
}

double annualizationFactorForForwardDays(int forwardDays)
{
    return 252.0 / static_cast<double>((std::max)(1, forwardDays));
}

double populationStdDev(const std::vector<double>& values)
{
    if (values.empty()) {
        return 0.0;
    }

    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    double squaredDiffSum = 0.0;
    for (double value : values) {
        const double diff = value - mean;
        squaredDiffSum += diff * diff;
    }

    return std::sqrt(squaredDiffSum / static_cast<double>(values.size()));
}

double monotonicityScore(const std::vector<double>& values)
{
    if (values.size() < 2) {
        return 0.0;
    }

    const size_t count = values.size();
    const double meanX = (static_cast<double>(count) + 1.0) / 2.0;
    const double meanY = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(count);

    double covariance = 0.0;
    double varianceX = 0.0;
    double varianceY = 0.0;
    for (size_t index = 0; index < count; ++index) {
        const double x = static_cast<double>(index + 1);
        const double dx = x - meanX;
        const double dy = values[index] - meanY;
        covariance += dx * dy;
        varianceX += dx * dx;
        varianceY += dy * dy;
    }

    if (varianceX <= 0.0 || varianceY <= 0.0) {
        return 0.0;
    }

    return covariance / std::sqrt(varianceX * varianceY);
}

QVariantList buildGroupResultList(const factor::BacktestResult& result)
{
    QVariantList groups;
    const double annualizationFactor = annualizationFactorForForwardDays(result.config.forwardDays);
    const size_t groupCount = result.groupResult.groupReturns.size();
    groups.reserve(static_cast<int>(groupCount));

    for (size_t index = 0; index < groupCount; ++index) {
        QVariantMap group;
        const double groupReturn = result.groupResult.groupReturns[index];
        group[QStringLiteral("groupIndex")] = static_cast<int>(index + 1);
        group[QStringLiteral("returnRate")] = groupReturn;
        group[QStringLiteral("annualizedReturn")] = groupReturn * annualizationFactor;
        if (index < result.groupResult.groupStockCounts.size()) {
            group[QStringLiteral("stockCount")] = result.groupResult.groupStockCounts[index];
        }
        if (index < result.groupResult.minFactorValues.size()) {
            group[QStringLiteral("minFactorValue")] = result.groupResult.minFactorValues[index];
        }
        if (index < result.groupResult.maxFactorValues.size()) {
            group[QStringLiteral("maxFactorValue")] = result.groupResult.maxFactorValues[index];
        }
        groups.append(group);
    }

    return groups;
}

QVariantMap buildIcirResultMap(const factor::BacktestResult& result)
{
    QVariantMap icirResult;
    const double displayedIcValue = std::fabs(result.icirResult.ir) > 1.0
        ? result.icirResult.ir
        : result.icirResult.icMean;
    icirResult[QStringLiteral("icValue")] = displayedIcValue;
    icirResult[QStringLiteral("icStd")] = result.icirResult.icStd;
    icirResult[QStringLiteral("irValue")] = result.icirResult.ir;
    icirResult[QStringLiteral("icPositiveRate")] = result.icirResult.icPositiveRatio;
    return icirResult;
}

QVariantMap buildSummaryResultMap(const factor::BacktestResult& result)
{
    QVariantMap summary;
    summary[QStringLiteral("spreadReturn")] = result.groupResult.longShortReturn;
    summary[QStringLiteral("longShortAnnualReturn")] = result.annualReturn;
    summary[QStringLiteral("dataCoverage")] = result.dataCoverage;
    summary[QStringLiteral("sharpeRatio")] = result.sharpeRatio;
    summary[QStringLiteral("maxDrawdown")] = result.maxDrawdown;
    summary[QStringLiteral("winRate")] = result.winRate;
    summary[QStringLiteral("profitFactor")] = result.profitFactor;
    summary[QStringLiteral("turnoverRate")] = result.turnoverRate;
    summary[QStringLiteral("benchmarkAnnualReturn")] = result.benchmarkAnnualReturn;
    summary[QStringLiteral("excessAnnualReturn")] = result.excessAnnualReturn;
    summary[QStringLiteral("trackingError")] = result.trackingError;
    summary[QStringLiteral("informationRatio")] = result.informationRatio;
    summary[QStringLiteral("alpha")] = result.alpha;
    summary[QStringLiteral("beta")] = result.beta;
    summary[QStringLiteral("monotonicity")] = monotonicityScore(result.groupResult.groupReturns);
    summary[QStringLiteral("discrimination")] = populationStdDev(result.groupResult.groupReturns);
    return summary;
}

QVariantMap buildConfigMap(const QString& requestedFactorId,
                          const factor::BacktestResult& result)
{
    QVariantMap config;
    config[QStringLiteral("factorId")] = requestedFactorId.trimmed();
    config[QStringLiteral("factorName")] = QString::fromStdString(result.instanceName);
    config[QStringLiteral("instanceId")] = QString::fromStdString(result.instanceId);
    config[QStringLiteral("startDate")] = QString::fromStdString(result.config.startDate);
    config[QStringLiteral("endDate")] = QString::fromStdString(result.config.endDate);
    config[QStringLiteral("numGroups")] = result.config.numGroups;
    risk::config::setForwardDays(config, result.config.forwardDays);
    risk::config::setRebalanceDays(config, result.config.rebalanceDays);
    risk::config::setCommissionRate(config, result.config.transactionCost);
    risk::config::setSlippageRate(config, result.config.slippageRate);
    risk::config::setRiskFreeRate(config, result.config.riskFreeRate);
    risk::config::setBenchmarkSymbol(config, QString::fromStdString(result.config.benchmarkSymbol));
    config[QStringLiteral("datasetId")] = result.config.datasetId;
    return config;
}

QString persistedResultFilePathForController()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty()) {
        root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    if (root.isEmpty()) {
        root = QDir::currentPath();
    }
    return QDir(root).filePath(QStringLiteral("factor_backtest_result.json"));
}

bool configNeutralizationEnabled(const factor::FactorInstanceInfo& info)
{
    if (!info.config.has("calculation")) {
        return false;
    }
    const auto calculation = info.config.get("calculation");
    return calculation.has("neutralizationEnabled") && calculation.get("neutralizationEnabled").asBool();
}

void appendConfigStringField(QStringList& fields, const foundation::json::JsonFacade& value)
{
    if (!value.isString()) {
        return;
    }

    const QString normalized = QString::fromStdString(value.asString()).trimmed();
    if (!normalized.isEmpty()) {
        fields.append(normalized);
    }
}

QStringList declaredRequiredFieldsFromConfig(const factor::FactorInstanceInfo& info)
{
    QStringList fields;

    if (!info.config.has("dataRequirements")) {
        return fields;
    }

    const auto dataRequirements = info.config.get("dataRequirements");
    if (!dataRequirements.has("required") || !dataRequirements.get("required").isArray()) {
        return fields;
    }

    const auto required = dataRequirements.get("required");
    for (size_t index = 0; index < required.size(); ++index) {
        appendConfigStringField(fields, required.at(index));
    }

    return dedupeStringList(fields);
}

QVariantMap buildSupportInfo(const QString& factorId,
                             const QString& instanceId,
                             factor::FactorType runtimeType,
                             const QString& category,
                             const QString& reason,
                             const QStringList& requiredFields,
                             const QStringList& missingFields,
                             factor::SourceTable sourceTable,
                             bool supported)
{
    QVariantMap info;
    info[QStringLiteral("factorId")] = factorId.trimmed();
    info[QStringLiteral("instanceId")] = instanceId.trimmed();
    info[QStringLiteral("runtimeType")] = static_cast<int>(runtimeType);
    info[QStringLiteral("supported")] = supported;
    info[QStringLiteral("category")] = category;
    info[QStringLiteral("reason")] = reason;
    info[QStringLiteral("requiredFields")] = toVariantList(requiredFields);
    info[QStringLiteral("missingFields")] = toVariantList(missingFields);
    info[QStringLiteral("sourceTable")] = static_cast<int>(sourceTable);
    return info;
}

QVariantMap buildFailureFromSupportInfo(const QVariantMap& supportInfo)
{
    QVariantMap failure;
    failure[QStringLiteral("factorId")] = supportInfo.value(QStringLiteral("factorId")).toString().trimmed();
    failure[QStringLiteral("instanceId")] = supportInfo.value(QStringLiteral("instanceId")).toString().trimmed();
    failure[QStringLiteral("reason")] = supportInfo.value(QStringLiteral("reason")).toString().trimmed();
    failure[QStringLiteral("category")] = supportInfo.value(QStringLiteral("category")).toString().trimmed();
    failure[QStringLiteral("sourceTable")] = supportInfo.value(QStringLiteral("sourceTable"));
    failure[QStringLiteral("missingFields")] = supportInfo.value(QStringLiteral("missingFields")).toList();
    return failure;
}

QVariantMap categoryMetaTemplate(const QString& key,
                                 const QString& statusText,
                                 const QString& shortText,
                                 const QString& detail,
                                 const QString& accentColor,
                                 const QString& chipBackground,
                                 const QString& chipBorder,
                                 const QString& chipText)
{
    QVariantMap meta;
    meta[QStringLiteral("key")] = key;
    meta[QStringLiteral("statusText")] = statusText;
    meta[QStringLiteral("shortText")] = shortText;
    meta[QStringLiteral("detail")] = detail;
    meta[QStringLiteral("accentColor")] = accentColor;
    meta[QStringLiteral("chipBackground")] = chipBackground;
    meta[QStringLiteral("chipBorder")] = chipBorder;
    meta[QStringLiteral("chipText")] = chipText;
    return meta;
}

} // namespace

FactorBacktestController::FactorBacktestController(QObject *parent)
    : QObject(parent)
    , m_status(removedReason())
{
    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(120);
    connect(m_progressTimer, &QTimer::timeout, this, &FactorBacktestController::pollBacktestProgress);
    refreshBacktestRuntimeParamsFromRiskConfiguration();
}

FactorBacktestController::~FactorBacktestController()
{
    shutdownBacktestInfrastructure();
}

void FactorBacktestController::setSelectedFactorIds(const QVariantList& factorIds)
{
    const QVariantList normalized = dedupeFactorIds(factorIds);
    if (m_selectedFactorIds == normalized) {
        return;
    }

    m_selectedFactorIds = normalized;
    emit selectedFactorIdsChanged(m_selectedFactorIds);

    m_lastPreflightFailures.clear();
    m_factorSupportMapCache = buildFactorSupportMap(m_selectedFactorIds);
    emit factorSupportMapCacheChanged(m_factorSupportMapCache);
    for (const QVariant& factorIdValue : m_selectedFactorIds) {
        const QVariantMap supportInfo = m_factorSupportMapCache.value(factorIdValue.toString().trimmed()).toMap();
        if (!supportInfo.isEmpty() && !supportInfo.value(QStringLiteral("supported")).toBool()) {
            m_lastPreflightFailures.append(buildFailureFromSupportInfo(supportInfo));
        }
    }
    emit lastPreflightFailuresChanged(m_lastPreflightFailures);
}

void FactorBacktestController::setSelectedDatasetId(int datasetId)
{
    if (m_selectedDatasetId == datasetId) {
        return;
    }
    m_selectedDatasetId = datasetId;
    emit selectedDatasetIdChanged(m_selectedDatasetId);
}

void FactorBacktestController::setSelectedDatasetBenchmarkMetadata(const QVariantMap& metadata)
{
    if (m_selectedDatasetBenchmarkMetadata == metadata) {
        return;
    }
    m_selectedDatasetBenchmarkMetadata = metadata;
    emit selectedDatasetBenchmarkMetadataChanged(m_selectedDatasetBenchmarkMetadata);
}

void FactorBacktestController::setDataSourceMode(const QString& dataSourceMode)
{
    const QString normalizedMode = dataSourceMode.trimmed();
    if (m_dataSourceMode == normalizedMode) {
        return;
    }
    m_dataSourceMode = normalizedMode;
    emit dataSourceModeChanged(m_dataSourceMode);
}

void FactorBacktestController::setSelectedStockPoolSymbols(const QVariantList& stockPoolSymbols)
{
    const QVariantList normalized = normalizedStockPoolSymbols(stockPoolSymbols);
    if (m_selectedStockPoolSymbols == normalized) {
        return;
    }
    m_selectedStockPoolSymbols = normalized;
    emit selectedStockPoolSymbolsChanged(m_selectedStockPoolSymbols);
}

void FactorBacktestController::setBacktestRuntimeParams(const QVariantMap& backtestRuntimeParams)
{
    if (m_backtestRuntimeParams == backtestRuntimeParams) {
        return;
    }
    m_backtestRuntimeParams = backtestRuntimeParams;
    emit backtestRuntimeParamsChanged(m_backtestRuntimeParams);
}

void FactorBacktestController::refreshBacktestRuntimeParamsFromRiskConfiguration()
{
    QVariantMap params = m_loadAppliedRiskConfigOverrideForTests
        ? m_loadAppliedRiskConfigOverrideForTests()
        : m_backtestRuntimeParams;
    setBacktestRuntimeParams(params);
}

void FactorBacktestController::startBacktest(const QString& groupText,
                                            const QString& startDate,
                                            const QString& endDate,
                                            const QVariantMap& cacheSnapshot)
{
    startBacktestWithFactors(m_selectedFactorIds, groupText, startDate, endDate, cacheSnapshot);
}

void FactorBacktestController::startBacktestWithFactors(const QVariantList& factorIds,
                                                        const QString& groupText,
                                                        const QString& startDate,
                                                        const QString& endDate,
                                                        const QVariantMap& cacheSnapshot)
{
    Q_UNUSED(cacheSnapshot)

    const QVariantList normalizedFactorIds = dedupeFactorIds(factorIds);
    setSelectedFactorIds(normalizedFactorIds);
    if (normalizedFactorIds.isEmpty()) {
        finalizeBacktestFailure(QStringLiteral("未选择可执行回测的因子"), false);
        return;
    }

    if (!initializeRuntime() || !m_executor) {
        finalizeBacktestFailure(QStringLiteral("因子回测运行时未初始化"), false);
        return;
    }

    resetBatchState();
    resetResults();
    m_batchFactorIds = normalizedFactorIds;
    m_batchResultMaps.resize(static_cast<size_t>(normalizedFactorIds.size()));
    m_activeFactorIndex = 0;
    m_pendingGroupText = groupText;
    m_pendingStartDate = startDate;
    m_pendingEndDate = endDate;
    m_isRunning = true;
    m_progress = 0;
    m_status = QStringLiteral("正在回测");
    emit isRunningChanged(m_isRunning);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);

    try {
        for (int index = 0; index < normalizedFactorIds.size(); ++index) {
            const QString requestedFactorId = normalizedFactorIds.at(index).toString().trimmed();
            emit backtestStarted(requestedFactorId);

            const QString resolvedInstanceId = resolveInstanceId(requestedFactorId);
            const factor::BacktestConfig config = buildBacktestConfig(resolvedInstanceId, groupText, startDate, endDate);
            const factor::BacktestResult result = m_executor->execute(config);
            if (result.status != "SUCCESS") {
                const QString errorText = QString::fromStdString(result.errorMessage).trimmed();
                finalizeBacktestFailure(errorText.isEmpty() ? QStringLiteral("因子回测执行失败") : errorText, false);
                return;
            }

            finalizeBacktestSuccess(requestedFactorId, result, static_cast<size_t>(index));
        }
    } catch (const std::exception& e) {
        finalizeBacktestFailure(QString::fromUtf8(e.what()), false);
    }
}

QVariantMap FactorBacktestController::buildFactorSupportMap(const QVariantList& factorIds,
                                                           const QString& startDate,
                                                           const QString& endDate,
                                                           const QVariantMap& cacheSnapshot)
{
    QVariantMap supportMap;
    const QVariantList normalized = dedupeFactorIds(factorIds);
    const QString sourceMode = normalizedDataSourceMode(m_dataSourceMode);
    const bool useCacheMode = sourceMode != QStringLiteral("database");
    const bool hasPartialBacktestWindow = startDate.trimmed().isEmpty() != endDate.trimmed().isEmpty();

    DataServiceCache::DataSetInfo dataSetInfo;
    QVariantList dataSetRows;
    bool hasValidDataSet = false;

    if (useCacheMode) {
        auto& cache = DataServiceCache::getInstance();
        cache.initializeCache();
        if (m_selectedDatasetId > 0) {
            dataSetInfo = cache.getDataSetInfo(m_selectedDatasetId);
            hasValidDataSet = dataSetInfo.id > 0;
            if (hasValidDataSet && cacheSnapshot.value(QStringLiteral("tradeDateCount")).toInt() <= 0) {
                dataSetRows = cache.getDataSetById(m_selectedDatasetId);
            }
        }
    }

    const QSet<QString> availableFields = collectAvailableFields(cacheSnapshot, dataSetInfo, dataSetRows);
    const int availableTradeDateCount = cacheSnapshot.value(QStringLiteral("tradeDateCount")).toInt() > 0
        ? cacheSnapshot.value(QStringLiteral("tradeDateCount")).toInt()
        : uniqueTradeDateCount(dataSetRows);

    for (const QVariant& factorIdValue : normalized) {
        const QString factorId = factorIdValue.toString().trimmed();
        const QString resolvedInstanceId = resolveInstanceId(factorIdValue);
        if (resolvedInstanceId.trimmed().isEmpty()) {
            supportMap.insert(factorId, buildSupportInfo(
                factorId,
                {},
                factor::FactorType::UNKNOWN,
                QStringLiteral("instance-missing"),
                QStringLiteral("未找到对应的因子实例 ID"),
                {},
                {},
                factor::SourceTable::UNKNOWN,
                false));
            continue;
        }

        const factor::FactorInstanceInfo info = getInstanceInfo(resolvedInstanceId);
        std::shared_ptr<factor::BaseFactor> factorInstance;
        if (m_factorInstanceOverrideForTests) {
            factorInstance = m_factorInstanceOverrideForTests(resolvedInstanceId);
        } else if (m_instanceManager) {
            factorInstance = m_instanceManager->createInstance(resolvedInstanceId.toStdString());
        }

        const factor::FactorType runtimeType = resolveRuntimeType(info, factorInstance);
        if (!factorInstance) {
            supportMap.insert(factorId, buildSupportInfo(
                factorId,
                resolvedInstanceId,
                runtimeType,
                QStringLiteral("instance-create-failed"),
                QStringLiteral("因子实例创建失败，无法执行因子检查"),
                {},
                {},
                factor::SourceTable::UNKNOWN,
                false));
            continue;
        }

        if (!useCacheMode && hasPartialBacktestWindow) {
            supportMap.insert(factorId, buildSupportInfo(
                factorId,
                resolvedInstanceId,
                runtimeType,
                QStringLiteral("invalid-backtest-window"),
                QStringLiteral("回测开始/结束日期必须同时提供，禁止使用默认兜底日期"),
                {},
                {},
                factor::SourceTable::UNKNOWN,
                false));
            continue;
        }

        factor::DataRequirements requirements = factorInstance->getDataRequirements();
        const factor::BoundaryRules boundaryRules = factorInstance->getBoundaryRules();
        const QStringList explicitConfigRequiredFields = declaredRequiredFieldsFromConfig(info);
        if (requirements.requiredFields.empty() && !explicitConfigRequiredFields.isEmpty()) {
            requirements.requiredFields.clear();
            for (const QString& field : explicitConfigRequiredFields) {
                requirements.requiredFields.push_back(field.toStdString());
            }
        } else if (runtimeType == factor::FactorType::DIVIDEND && !explicitConfigRequiredFields.isEmpty()) {
            requirements.requiredFields.clear();
            for (const QString& field : explicitConfigRequiredFields) {
                requirements.requiredFields.push_back(field.toStdString());
            }
        }
        if (runtimeType == factor::FactorType::DIVIDEND && explicitConfigRequiredFields.isEmpty()) {
            const auto appendRequirementField = [&requirements](const QString& field) {
                const std::string normalized = field.toStdString();
                if (std::find(requirements.requiredFields.begin(), requirements.requiredFields.end(), normalized)
                    == requirements.requiredFields.end()) {
                    requirements.requiredFields.push_back(normalized);
                }
            };
            appendRequirementField(factor::bridge::MarketBarFieldKeys::PRE_ADJ_FACTOR);
            appendRequirementField(factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR);
        }
        if (configNeutralizationEnabled(info)) {
            const auto appendRequirementField = [&requirements](const QString& field) {
                const std::string normalized = field.toStdString();
                if (std::find(requirements.requiredFields.begin(), requirements.requiredFields.end(), normalized)
                    == requirements.requiredFields.end()) {
                    requirements.requiredFields.push_back(normalized);
                }
            };
            appendRequirementField(factor::bridge::MarketBarFieldKeys::INDUSTRY_CODE);
            appendRequirementField(factor::bridge::MarketBarFieldKeys::MARKET_CAP);
        }
        const QStringList requiredFields = normalizedRequiredFields(runtimeType, requirements);
        const factor::SourceTable sourceTable = requirements.sourceTable;

        if (runtimeType == factor::FactorType::CUSTOM && !configHasCustomExpression(info)) {
            supportMap.insert(factorId, buildSupportInfo(
                factorId,
                resolvedInstanceId,
                runtimeType,
                QStringLiteral("missing-field"),
                QStringLiteral("自定义因子必须显式提供 expression"),
                requiredFields,
                {},
                sourceTable,
                false));
            continue;
        }

        if (requiredFields.isEmpty()) {
            supportMap.insert(factorId, buildSupportInfo(
                factorId,
                resolvedInstanceId,
                runtimeType,
                QStringLiteral("missing-field"),
                QStringLiteral("因子未显式声明可用于回测检查的字段需求"),
                {},
                {},
                sourceTable,
                false));
            continue;
        }

        if (!useCacheMode) {
            supportMap.insert(factorId, buildSupportInfo(
                factorId,
                resolvedInstanceId,
                runtimeType,
                QStringLiteral("supported"),
                QStringLiteral("因子字段检查通过"),
                requiredFields,
                {},
                sourceTable,
                true));
            continue;
        }

        if (m_selectedDatasetId <= 0) {
            supportMap.insert(factorId, buildSupportInfo(
                factorId,
                resolvedInstanceId,
                runtimeType,
                QStringLiteral("dataset-missing"),
                QStringLiteral("未选择可用于因子回测检查的缓存集"),
                requiredFields,
                {},
                sourceTable,
                false));
            continue;
        }

        if (!hasValidDataSet) {
            supportMap.insert(factorId, buildSupportInfo(
                factorId,
                resolvedInstanceId,
                runtimeType,
                QStringLiteral("dataset-invalid"),
                QStringLiteral("所选缓存集不存在或元数据无效"),
                requiredFields,
                {},
                sourceTable,
                false));
            continue;
        }

        if (dataSetInfo.rowCount <= 0 && dataSetRows.isEmpty()) {
            supportMap.insert(factorId, buildSupportInfo(
                factorId,
                resolvedInstanceId,
                runtimeType,
                QStringLiteral("dataset-empty"),
                QStringLiteral("所选缓存集没有可用于因子检查的数据"),
                requiredFields,
                {},
                sourceTable,
                false));
            continue;
        }

        QStringList missingFields;
        for (const QString& requiredField : requiredFields) {
            if (!availableFields.contains(requiredField)) {
                missingFields.append(requiredField);
            }
        }
        missingFields = dedupeStringList(missingFields);
        if (!missingFields.isEmpty()) {
            supportMap.insert(factorId, buildSupportInfo(
                factorId,
                resolvedInstanceId,
                runtimeType,
                QStringLiteral("missing-field"),
                QStringLiteral("缓存集缺少因子检查所需字段: %1").arg(missingFields.join(QStringLiteral("、"))),
                requiredFields,
                missingFields,
                sourceTable,
                false));
            continue;
        }

        const int requiredWarmupTradingDays = m_requiredWarmupTradingDaysOverrideForTests.contains(resolvedInstanceId)
            ? std::max(1, m_requiredWarmupTradingDaysOverrideForTests.value(resolvedInstanceId))
            : 1;
        if (availableTradeDateCount > 0 && availableTradeDateCount < requiredWarmupTradingDays) {
            supportMap.insert(factorId, buildSupportInfo(
                factorId,
                resolvedInstanceId,
                runtimeType,
                QStringLiteral("insufficient-history"),
                QStringLiteral("缓存集仅覆盖 %1 个交易日，低于该因子所需的 %2 个交易日")
                    .arg(availableTradeDateCount)
                    .arg(requiredWarmupTradingDays),
                requiredFields,
                {},
                sourceTable,
                false));
            continue;
        }

        supportMap.insert(factorId, buildSupportInfo(
            factorId,
            resolvedInstanceId,
            runtimeType,
            QStringLiteral("supported"),
            QStringLiteral("字段与历史窗口检查通过"),
            requiredFields,
            {},
            sourceTable,
            true));
    }
    return supportMap;
}

void FactorBacktestController::requestFactorSupportMapAsync(const QVariantList& factorIds,
                                                            const QString& startDate,
                                                            const QString& endDate,
                                                            const QVariantMap& cacheSnapshot,
                                                            quint64 requestId)
{
    m_supportMapRequestInFlight = true;
    emit supportMapRequestInFlightChanged(true);

    const QVariantMap supportMap = buildFactorSupportMap(factorIds, startDate, endDate, cacheSnapshot);
    m_factorSupportMapCache = supportMap;
    emit factorSupportMapCacheChanged(m_factorSupportMapCache);
    emit factorSupportMapReady(requestId, supportMap);

    m_supportMapRequestInFlight = false;
    emit supportMapRequestInFlightChanged(false);
}

QVariantMap FactorBacktestController::preflightCategoryMeta(const QString& category) const
{
    const QString key = category.trimmed().isEmpty() ? QStringLiteral("unsupported") : category.trimmed();
    if (key == QStringLiteral("supported")) {
        return categoryMetaTemplate(
            key,
            QStringLiteral("可回测"),
            QStringLiteral("通过"),
            QStringLiteral("字段与历史窗口检查通过"),
            QStringLiteral("#15803d"),
            QStringLiteral("#ecfdf5"),
            QStringLiteral("#86efac"),
            QStringLiteral("#166534"));
    }
    if (key == QStringLiteral("dataset-missing")) {
        return categoryMetaTemplate(
            key,
            QStringLiteral("待选缓存集"),
            QStringLiteral("待选择"),
            QStringLiteral("需要先选择可用于因子检查的缓存集"),
            QStringLiteral("#1d4ed8"),
            QStringLiteral("#eff6ff"),
            QStringLiteral("#bfdbfe"),
            QStringLiteral("#1d4ed8"));
    }
    if (key == QStringLiteral("missing-field")) {
        return categoryMetaTemplate(
            key,
            QStringLiteral("字段缺失"),
            QStringLiteral("缺字段"),
            QStringLiteral("缓存集缺少因子所需字段"),
            QStringLiteral("#b45309"),
            QStringLiteral("#fff7ed"),
            QStringLiteral("#fed7aa"),
            QStringLiteral("#9a3412"));
    }
    if (key == QStringLiteral("insufficient-history")) {
        return categoryMetaTemplate(
            key,
            QStringLiteral("历史不足"),
            QStringLiteral("历史不足"),
            QStringLiteral("缓存集历史长度不足以支撑该因子"),
            QStringLiteral("#b91c1c"),
            QStringLiteral("#fef2f2"),
            QStringLiteral("#fecaca"),
            QStringLiteral("#991b1b"));
    }
    if (key == QStringLiteral("invalid-backtest-window")) {
        return categoryMetaTemplate(
            key,
            QStringLiteral("窗口无效"),
            QStringLiteral("窗口无效"),
            QStringLiteral("回测开始/结束日期必须同时提供"),
            QStringLiteral("#b91c1c"),
            QStringLiteral("#fef2f2"),
            QStringLiteral("#fecaca"),
            QStringLiteral("#991b1b"));
    }
    if (key == QStringLiteral("unsupported-metric")) {
        return categoryMetaTemplate(
            key,
            QStringLiteral("指标不支持"),
            QStringLiteral("不支持"),
            QStringLiteral("当前因子配置不支持进入回测检查"),
            QStringLiteral("#7c2d12"),
            QStringLiteral("#fff7ed"),
            QStringLiteral("#fdba74"),
            QStringLiteral("#9a3412"));
    }
    if (key == QStringLiteral("dataset-empty") || key == QStringLiteral("dataset-invalid")) {
        return categoryMetaTemplate(
            key,
            QStringLiteral("缓存集无效"),
            QStringLiteral("缓存集无效"),
            QStringLiteral("所选缓存集无法用于因子检查"),
            QStringLiteral("#b91c1c"),
            QStringLiteral("#fef2f2"),
            QStringLiteral("#fecaca"),
            QStringLiteral("#991b1b"));
    }
    if (key == QStringLiteral("instance-missing") || key == QStringLiteral("instance-create-failed")) {
        return categoryMetaTemplate(
            key,
            QStringLiteral("实例异常"),
            QStringLiteral("实例异常"),
            QStringLiteral("因子实例不可用，无法执行检查"),
            QStringLiteral("#b91c1c"),
            QStringLiteral("#fef2f2"),
            QStringLiteral("#fecaca"),
            QStringLiteral("#991b1b"));
    }

    return categoryMetaTemplate(
        key,
        QStringLiteral("不可回测"),
        QStringLiteral("不可回测"),
        removedReason(),
        QStringLiteral("#6b7280"),
        QStringLiteral("#f3f4f6"),
        QStringLiteral("#d1d5db"),
        QStringLiteral("#374151"));
}

QString FactorBacktestController::preflightFailureDetailText(const QVariantMap& failure,
                                                             const QString& factorDisplayName) const
{
    const QString reason = failure.value(QStringLiteral("reason")).toString().trimmed();
    if (!reason.isEmpty()) {
        return reason;
    }
    const QString category = failure.value(QStringLiteral("category")).toString().trimmed();
    const QVariantMap meta = preflightCategoryMeta(category);
    if (!factorDisplayName.trimmed().isEmpty()) {
        return QStringLiteral("%1: %2").arg(factorDisplayName.trimmed(), meta.value(QStringLiteral("detail")).toString());
    }
    return meta.value(QStringLiteral("detail")).toString();
}

QVariantMap FactorBacktestController::factorValidationState(const QString& factorId,
                                                            const QString& factorDisplayName,
                                                            bool hasFactorDefinition,
                                                            const QVariantMap& supportInfo,
                                                            const QVariantList& preflightFailures,
                                                            const QVariantMap& backtestResult,
                                                            const QString& lastBacktestError,
                                                            const QVariantList& selectedFactorIds,
                                                            const QString& dataSourceMode,
                                                            bool hasAvailableCacheDataset,
                                                            int selectedDatasetId) const
{
    QVariantMap state;
    state[QStringLiteral("factorId")] = factorId.trimmed();

    const QString normalizedFactorId = factorId.trimmed();
    const bool isSelected = selectedFactorIds.contains(normalizedFactorId);
    if (!hasFactorDefinition) {
        state[QStringLiteral("supported")] = false;
        state[QStringLiteral("category")] = QStringLiteral("instance-missing");
        state[QStringLiteral("reason")] = QStringLiteral("未找到因子定义或实例配置");
    } else if (!isSelected) {
        state[QStringLiteral("supported")] = false;
        state[QStringLiteral("category")] = QStringLiteral("unselected");
        state[QStringLiteral("reason")] = QStringLiteral("当前因子未加入回测检查列表");
    } else if (normalizedDataSourceMode(dataSourceMode) == QStringLiteral("cache") && !hasAvailableCacheDataset && selectedDatasetId <= 0) {
        state[QStringLiteral("supported")] = false;
        state[QStringLiteral("category")] = QStringLiteral("dataset-missing");
        state[QStringLiteral("reason")] = QStringLiteral("请先选择可用于因子回测检查的缓存集");
    } else if (!supportInfo.isEmpty()) {
        state[QStringLiteral("supported")] = supportInfo.value(QStringLiteral("supported")).toBool();
        state[QStringLiteral("category")] = supportInfo.value(QStringLiteral("category")).toString();
        state[QStringLiteral("reason")] = supportInfo.value(QStringLiteral("reason")).toString();
    } else if (!preflightFailures.isEmpty()) {
        const QVariantMap failure = preflightFailures.first().toMap();
        state[QStringLiteral("supported")] = false;
        state[QStringLiteral("category")] = failure.value(QStringLiteral("category")).toString();
        state[QStringLiteral("reason")] = preflightFailureDetailText(failure, factorDisplayName);
    } else if (!lastBacktestError.trimmed().isEmpty()) {
        state[QStringLiteral("supported")] = false;
        state[QStringLiteral("category")] = QStringLiteral("runtime-init-failed");
        state[QStringLiteral("reason")] = lastBacktestError.trimmed();
    } else if (!backtestResult.isEmpty()) {
        state[QStringLiteral("supported")] = true;
        state[QStringLiteral("category")] = QStringLiteral("supported");
        state[QStringLiteral("reason")] = QStringLiteral("该因子已有回测结果，可继续复用当前检查结论");
    } else {
        state[QStringLiteral("supported")] = false;
        state[QStringLiteral("category")] = QStringLiteral("unsupported");
        state[QStringLiteral("reason")] = removedReason();
    }

    const QVariantMap meta = preflightCategoryMeta(state.value(QStringLiteral("category")).toString());
    state[QStringLiteral("statusText")] = meta.value(QStringLiteral("statusText")).toString();
    state[QStringLiteral("shortText")] = meta.value(QStringLiteral("shortText")).toString();
    state[QStringLiteral("detail")] = meta.value(QStringLiteral("detail")).toString();
    state[QStringLiteral("accentColor")] = meta.value(QStringLiteral("accentColor")).toString();
    state[QStringLiteral("chipBackground")] = meta.value(QStringLiteral("chipBackground")).toString();
    state[QStringLiteral("chipBorder")] = meta.value(QStringLiteral("chipBorder")).toString();
    state[QStringLiteral("chipText")] = meta.value(QStringLiteral("chipText")).toString();
    state[QStringLiteral("displayName")] = factorDisplayName.trimmed();
    return state;
}

bool FactorBacktestController::datasetSelectableForBacktest(const QVariantMap& dataset) const
{
    const int datasetId = dataset.value(QStringLiteral("id"), dataset.value(QStringLiteral("value"))).toInt();
    if (datasetId <= 0) {
        return false;
    }

    const bool isBacktestReady = dataset.value(QStringLiteral("isBacktestReady")).toBool();
    const QStringList tags = dataset.value(QStringLiteral("tags")).toStringList();
    if (!isBacktestReady && !tags.contains(QStringLiteral("factor_backtest_ready"))) {
        return false;
    }

    const QStringList availableFields = dataset.value(QStringLiteral("availableFields")).toStringList();
    return !availableFields.isEmpty();
}

QVariantList FactorBacktestController::buildBacktestDatasetOptions(const QVariantList& datasetList) const
{
    QVariantList options;
    options.append(QVariantMap{
        {QStringLiteral("text"), QStringLiteral("请选择缓存集")},
        {QStringLiteral("value"), -1},
        {QStringLiteral("raw"), QVariantMap{}}
    });

    QList<QVariantMap> selectableDatasets;
    selectableDatasets.reserve(datasetList.size());
    for (const QVariant& datasetValue : datasetList) {
        const QVariantMap dataset = datasetValue.toMap();
        if (datasetSelectableForBacktest(dataset)) {
            selectableDatasets.append(dataset);
        }
    }

    std::sort(selectableDatasets.begin(), selectableDatasets.end(), [](const QVariantMap& left, const QVariantMap& right) {
        return left.value(QStringLiteral("id")).toInt() > right.value(QStringLiteral("id")).toInt();
    });

    for (const QVariantMap& dataset : selectableDatasets) {
        const int datasetId = dataset.value(QStringLiteral("id")).toInt();
        const QString displayName = dataset.value(QStringLiteral("displayName")).toString().trimmed();
        const QString startDate = dataset.value(QStringLiteral("startDate")).toString().trimmed();
        const QString endDate = dataset.value(QStringLiteral("endDate")).toString().trimmed();
        QString text = displayName.isEmpty()
            ? QStringLiteral("缓存集 #%1").arg(datasetId)
            : displayName;
        if (!startDate.isEmpty() && !endDate.isEmpty()) {
            text += QStringLiteral(" (%1 ~ %2)").arg(startDate, endDate);
        }
        options.append(QVariantMap{
            {QStringLiteral("text"), text},
            {QStringLiteral("value"), datasetId},
            {QStringLiteral("raw"), dataset}
        });
    }

    return options;
}

QVariantList FactorBacktestController::normalizeFactorIds(const QVariantList& factorIds) const
{
    return dedupeFactorIds(factorIds);
}

QVariantMap FactorBacktestController::filterFactorIdsBySupport(const QVariantList& factorIds,
                                                               const QVariantMap& supportMap) const
{
    QVariantMap result;
    QVariantList supportedFactorIds;
    QVariantList unsupportedFactorIds;

    const QVariantList normalized = dedupeFactorIds(factorIds);
    for (const QVariant& factorIdValue : normalized) {
        const QString factorId = factorIdValue.toString().trimmed();
        const QVariantMap supportInfo = supportMap.value(factorId).toMap();
        if (!supportInfo.isEmpty() && supportInfo.value(QStringLiteral("supported")).toBool()) {
            supportedFactorIds.append(factorId);
        } else {
            unsupportedFactorIds.append(factorId);
        }
    }

    result[QStringLiteral("supportedFactorIds")] = supportedFactorIds;
    result[QStringLiteral("unsupportedFactorIds")] = unsupportedFactorIds;
    result[QStringLiteral("filteredFactorIds")] = supportedFactorIds;
    return result;
}

int FactorBacktestController::beginFactorSupportMapRefresh(const QVariantList& factorIds,
                                                           const QString& startDate,
                                                           const QString& endDate,
                                                           const QVariantMap& cacheSnapshot)
{
    const int requestId = ++m_supportMapRequestSeq;
    requestFactorSupportMapAsync(factorIds, startDate, endDate, cacheSnapshot, static_cast<quint64>(requestId));
    return requestId;
}

bool FactorBacktestController::handleFactorSupportMapReady(int requestId,
                                                           const QVariantMap& supportMap)
{
    if (requestId < m_supportMapAppliedSeq) {
        return false;
    }

    m_supportMapAppliedSeq = requestId;
    m_factorSupportMapCache = supportMap;
    emit factorSupportMapCacheChanged(m_factorSupportMapCache);
    m_supportMapRequestInFlight = false;
    emit supportMapRequestInFlightChanged(false);
    return true;
}

void FactorBacktestController::markPendingFilterAfterSupportMap()
{
    m_pendingFilterAfterSupportMap = true;
}

bool FactorBacktestController::takePendingFilterAfterSupportMap()
{
    const bool pending = m_pendingFilterAfterSupportMap;
    m_pendingFilterAfterSupportMap = false;
    return pending;
}

QVariantMap FactorBacktestController::buildStockPoolComparison(const QVariantMap& previousBacktestReport,
                                                               const QVariantMap& currentDatasetInfo) const
{
    Q_UNUSED(previousBacktestReport)
    Q_UNUSED(currentDatasetInfo)
    return {};
}

QString FactorBacktestController::stockPoolComparisonText(const QVariantList& selectedFactorIds,
                                                          const QVariantMap& comparison) const
{
    Q_UNUSED(selectedFactorIds)
    Q_UNUSED(comparison)
    return removedReason();
}

QVariantList FactorBacktestController::displayedBacktestResults(const QVariantMap& backtestResult) const
{
    return backtestResult.value(QStringLiteral("results")).toList();
}

QString FactorBacktestController::displayedBacktestResultName(const QVariantMap& entry) const
{
    const QString displayName = entry.value(QStringLiteral("displayName")).toString().trimmed();
    if (!displayName.isEmpty()) {
        return displayName;
    }
    return entry.value(QStringLiteral("factorName")).toString().trimmed();
}

QVariantMap FactorBacktestController::resolveDisplayedBacktestState(const QVariantMap& backtestResult,
                                                                    int selectedResultIndex) const
{
    Q_UNUSED(selectedResultIndex)
    return backtestResult;
}

QVariantMap FactorBacktestController::resolveRiskConfigurationSnapshot(const QVariantMap& displayedBacktestResult,
                                                                      const QVariantMap& appliedConfiguration,
                                                                      const QVariantMap& currentConfiguration) const
{
    Q_UNUSED(displayedBacktestResult)
    Q_UNUSED(appliedConfiguration)
    Q_UNUSED(currentConfiguration)
    return {};
}

QString FactorBacktestController::riskConfigBenchmarkSymbol(const QVariantMap& snapshot,
                                                            const QString& fallbackSymbol) const
{
    const QString symbol = risk::config::benchmarkSymbol(snapshot, fallbackSymbol);
    return symbol.isEmpty() ? fallbackSymbol : symbol;
}

QVariantList FactorBacktestController::riskConfigMetricCards(const QVariantMap& snapshot) const
{
    Q_UNUSED(snapshot)
    return {};
}

QVariantMap FactorBacktestController::buildSingleFactorRunEntry(const QVariantMap& result,
                                                                const QString& fallbackFactorName) const
{
    QVariantMap entry = result;
    if (entry.value(QStringLiteral("factorName")).toString().trimmed().isEmpty()) {
        entry[QStringLiteral("factorName")] = fallbackFactorName;
    }
    return entry;
}

QVariantList FactorBacktestController::pushSingleFactorRunHistory(const QVariantList& existingHistory,
                                                                  const QVariantMap& result,
                                                                  int historyLimit,
                                                                  const QString& fallbackFactorName) const
{
    QVariantList history = existingHistory;
    history.prepend(buildSingleFactorRunEntry(result, fallbackFactorName));
    while (history.size() > historyLimit && historyLimit >= 0) {
        history.removeLast();
    }
    return history;
}

int FactorBacktestController::parseGroupCount(const QString& groupText) const
{
    QString digits;
    for (const QChar ch : groupText) {
        if (ch.isDigit()) {
            digits.append(ch);
        }
    }
    bool ok = false;
    const int parsed = digits.toInt(&ok);
    return ok && parsed > 0 ? parsed : 10;
}

bool FactorBacktestController::initializeRuntime()
{
    if (m_executor) {
        return true;
    }
    if (!m_instanceManager) {
        return false;
    }
    if (!m_threadPool) {
        m_threadPool = std::make_shared<foundation::thread::ThreadPoolExecutor>(1);
    }
    if (!m_cacheManager) {
        m_cacheManager = std::make_shared<factor::FactorCacheManager>();
    }
    m_executor = std::make_unique<factor::FactorBacktestExecutor>(m_instanceManager, m_threadPool, m_cacheManager);
    return true;
}

QString FactorBacktestController::resolveInstanceId(const QVariant& factorId) const
{
    if (m_resolveInstanceIdOverrideForTests) {
        return m_resolveInstanceIdOverrideForTests(factorId).trimmed();
    }

    const QString directId = factorId.toString().trimmed();
    if (!directId.isEmpty()) {
        return directId;
    }

    const QVariantMap factorMap = factorId.toMap();
    return factorMap.value(QStringLiteral("instanceId"), factorMap.value(QStringLiteral("factorId"))).toString().trimmed();
}

factor::FactorInstanceInfo FactorBacktestController::getInstanceInfo(const QString& resolvedInstanceId) const
{
    if (m_instanceInfoOverrideForTests) {
        return m_instanceInfoOverrideForTests(resolvedInstanceId);
    }
    if (m_instanceManager) {
        return m_instanceManager->getInstanceInfo(resolvedInstanceId.toStdString());
    }

    factor::FactorInstanceInfo info;
    info.instanceId = resolvedInstanceId.toStdString();
    return info;
}

factor::BacktestConfig FactorBacktestController::buildBacktestConfig(const QString& resolvedInstanceId,
                                                                     const QString& groupText,
                                                                     const QString& startDate,
                                                                     const QString& endDate) const
{
    const QString normalizedSourceMode = normalizedDataSourceMode(m_dataSourceMode);
    const QString trimmedStartDate = startDate.trimmed();
    const QString trimmedEndDate = endDate.trimmed();
    if (normalizedSourceMode == QStringLiteral("database")) {
        if (trimmedStartDate.isEmpty()) {
            throw std::runtime_error(QStringLiteral("回测开始日期缺失，禁止使用默认兜底日期").toUtf8().constData());
        }
        if (trimmedEndDate.isEmpty()) {
            throw std::runtime_error(QStringLiteral("回测结束日期缺失，禁止使用默认兜底日期").toUtf8().constData());
        }
    }

    factor::BacktestConfig config;
    config.instanceId = resolvedInstanceId.toStdString();
    config.datasetId = m_selectedDatasetId;
    config.startDate = trimmedStartDate.toStdString();
    config.endDate = trimmedEndDate.toStdString();
    config.numGroups = parseGroupCount(groupText);

    const QVariantMap runtimeParams = m_backtestRuntimeParams;
    config.forwardDays = risk::config::forwardDays(runtimeParams, 1);
    config.rebalanceDays = risk::config::rebalanceDays(runtimeParams, 1);
    config.transactionCost = risk::config::commissionRate(runtimeParams, 0.001);
    config.slippageRate = risk::config::slippageRate(runtimeParams, 0.0);
    config.riskFreeRate = risk::config::riskFreeRate(runtimeParams, 0.0);
    config.benchmarkSymbol = risk::config::benchmarkSymbol(runtimeParams, QStringLiteral("000300.SH")).toStdString();
    config.stopLossRate = risk::config::stopLossPercent(runtimeParams, 0.0);
    config.takeProfitRate = risk::config::takeProfitPercent(runtimeParams, 0.0);
    config.maxDrawdownLimit = risk::config::maxDrawdownLimit(runtimeParams, 0.0);
    config.maxDailyLoss = risk::config::maxDailyLoss(runtimeParams, 0.0);
    config.maxPositionPercent = risk::config::maxPositionPercent(runtimeParams, 1.0);
    config.maxTotalExposure = risk::config::maxTotalExposure(runtimeParams, 1.0);

    for (const QVariant& symbolValue : normalizedStockPoolSymbols(m_selectedStockPoolSymbols)) {
        config.allowedStockCodes.push_back(symbolValue.toString().toStdString());
    }

    if (normalizedSourceMode == QStringLiteral("cache") && m_selectedDatasetId > 0) {
        auto& cache = DataServiceCache::getInstance();
        cache.initializeCache();
        const QVariantList rows = cache.getDataSetById(m_selectedDatasetId);
        config.cachedBars.reserve(static_cast<size_t>(rows.size()));
        for (const QVariant& rowValue : rows) {
            const QVariantMap row = rowValue.toMap();
            const QString symbol = row.value(factor::bridge::CommonFieldKeys::SYMBOL).toString().trimmed();
            const QString tradeDate = row.value(
                factor::bridge::CommonFieldKeys::TRADE_DATE,
                row.value(factor::bridge::LegacyCleaningFieldKeys::DATE)).toString().trimmed();
            const double close = row.value(factor::bridge::MarketBarFieldKeys::CLOSE).toDouble();
            if (symbol.isEmpty() || tradeDate.isEmpty() || !std::isfinite(close)) {
                continue;
            }

            factor::CachedMarketBar bar;
            bar.symbol = symbol.toStdString();
            bar.tradeDate = tradeDate.toStdString();
            bar.close = close;
            bar.numericFields[factor::bridge::MarketBarFieldKeys::CLOSE.c_str()] = close;

            const QVariant adjFactorValue = row.contains(factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR)
                ? row.value(factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR)
                : row.value(factor::bridge::MarketBarFieldKeys::PRE_ADJ_FACTOR, 1.0);
            const double adjFactor = adjFactorValue.toDouble();
            if (std::isfinite(adjFactor)) {
                bar.numericFields[factor::bridge::LegacyCleaningFieldKeys::ADJ_FACTOR.c_str()] = adjFactor;
            }

            config.cachedBars.push_back(std::move(bar));
        }
    }

    return config;
}

QVariantMap FactorBacktestController::buildResultMap(const QString& requestedFactorId,
                                                     const factor::BacktestResult& result) const
{
    QVariantMap map;
    map[QStringLiteral("factorId")] = requestedFactorId.trimmed();
    map[QStringLiteral("taskId")] = QString::fromStdString(result.resultId.to_string());
    map[QStringLiteral("executionTime")] = result.executionTimeMs;
    map[QStringLiteral("success")] = (result.status == "SUCCESS");
    map[QStringLiteral("status")] = QString::fromStdString(result.status);
    map[QStringLiteral("turnoverRate")] = result.turnoverRate;
    map[QStringLiteral("config")] = buildConfigMap(requestedFactorId, result);
    map[QStringLiteral("groups")] = buildGroupResultList(result);
    map[QStringLiteral("icirResult")] = buildIcirResultMap(result);
    map[QStringLiteral("summary")] = buildSummaryResultMap(result);
    map[QStringLiteral("results")] = QVariantList{};
    map[QStringLiteral("factorIds")] = QVariantList{};
    return map;
}

QVariantMap FactorBacktestController::buildAggregatedResultMap() const
{
    QVariantList completedResults;
    completedResults.reserve(static_cast<int>(m_batchResultMaps.size()));
    int executionTime = 0;
    for (const QVariantMap& resultMap : m_batchResultMaps) {
        if (resultMap.isEmpty()) {
            continue;
        }
        completedResults.append(resultMap);
        executionTime += resultMap.value(QStringLiteral("executionTime")).toInt();
    }

    if (completedResults.isEmpty()) {
        return {};
    }

    if (completedResults.size() == 1) {
        QVariantMap single = completedResults.first().toMap();
        single[QStringLiteral("results")] = QVariantList{};
        single[QStringLiteral("factorIds")] = QVariantList{};
        return single;
    }

    QVariantMap aggregate = completedResults.first().toMap();
    aggregate[QStringLiteral("results")] = completedResults;
    aggregate[QStringLiteral("factorIds")] = dedupeFactorIds(m_batchFactorIds);
    aggregate[QStringLiteral("factorCount")] = completedResults.size();
    aggregate[QStringLiteral("executionTime")] = executionTime;
    return aggregate;
}

void FactorBacktestController::launchNextBacktestTask()
{
}

void FactorBacktestController::resetBatchState()
{
    m_pendingBacktestTasks.clear();
    m_batchFactorIds.clear();
    m_batchResultMaps.clear();
    m_activeFactorIndex = 0;
}

void FactorBacktestController::pollBacktestProgress()
{
}

void FactorBacktestController::finalizeBacktestSuccess(const QString& requestedFactorId,
                                                       const factor::BacktestResult& result,
                                                       size_t batchIndex)
{
    const QVariantMap resultMap = buildResultMap(requestedFactorId, result);
    if (batchIndex < m_batchResultMaps.size()) {
        m_batchResultMaps[batchIndex] = resultMap;
    }

    syncBacktestMetricsToFactor(requestedFactorId, result);

    m_activeFactorIndex = static_cast<int>((std::min)(batchIndex + 1, m_batchResultMaps.size()));
    const int totalFactors = (std::max)(1, static_cast<int>(m_batchFactorIds.size()));
    m_progress = static_cast<int>((100.0 * static_cast<double>(m_activeFactorIndex)) / static_cast<double>(totalFactors));
    emit progressChanged(m_progress);
    emit backtestProgress(m_progress, QStringLiteral("正在回测"));
    emit backtestProgressDetailed(m_progress, QStringLiteral("正在回测"), m_activeFactorIndex, totalFactors);

    const bool completedAll = !m_batchResultMaps.empty()
        && std::all_of(m_batchResultMaps.cbegin(), m_batchResultMaps.cend(), [](const QVariantMap& item) {
            return !item.isEmpty();
        });
    if (!completedAll) {
        return;
    }

    const QVariantMap aggregate = buildAggregatedResultMap();
    m_isRunning = false;
    m_progress = 100;
    m_status = QStringLiteral("回测完成");
    m_backtestResult = aggregate;
    m_groupResults = aggregate.value(QStringLiteral("groups")).toList();
    m_icirResult = aggregate.value(QStringLiteral("icirResult")).toMap();
    m_summaryStats = aggregate.value(QStringLiteral("summary")).toMap();
    emit isRunningChanged(m_isRunning);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    emit backtestResultChanged(m_backtestResult);
    emit groupResultsChanged(m_groupResults);
    emit icirResultChanged(m_icirResult);
    emit summaryStatsChanged(m_summaryStats);
    persistLatestResult();
    emit backtestCompleted(aggregate);
}

void FactorBacktestController::finalizeBacktestFailure(const QString& errorMessage,
                                                       bool cancelled)
{
    m_isRunning = false;
    m_progress = 0;
    m_status = errorMessage.trimmed().isEmpty() ? removedReason() : errorMessage.trimmed();
    emit isRunningChanged(m_isRunning);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    if (cancelled) {
        emit backtestCancelled();
    } else {
        emit backtestFailed(m_status);
    }
}

void FactorBacktestController::syncBacktestMetricsToFactor(const QString& requestedFactorId,
                                                           const factor::BacktestResult& result)
{
    if (requestedFactorId.trimmed().isEmpty() || result.status != "SUCCESS") {
        return;
    }

    const QVariantMap configuredThresholds = m_loadAppliedRiskConfigOverrideForTests
        ? m_loadAppliedRiskConfigOverrideForTests()
        : m_backtestRuntimeParams;
    const double minAbsIc = risk::config::metricPersistenceMinAbsIc(configuredThresholds, 0.03);
    const double minIr = risk::config::metricPersistenceMinIr(configuredThresholds, 0.0);
    const double minProfitFactor = risk::config::metricPersistenceMinProfitFactor(configuredThresholds, 1.5);

    if (std::abs(result.icirResult.icMean) < minAbsIc
        || result.icirResult.ir < minIr
        || result.profitFactor < minProfitFactor) {
        return;
    }

    FactorService* service = FactorService::instance();
    if (!service) {
        return;
    }

    QVariantMap factorData = service->getFactorById(requestedFactorId);
    if (factorData.isEmpty()) {
        factorData = service->getFactorByIdFromRepository(requestedFactorId);
    }
    factorData[QStringLiteral("factorId")] = requestedFactorId.trimmed();
    factorData[QStringLiteral("icValue")] = result.icirResult.icMean;
    factorData[QStringLiteral("irValue")] = result.icirResult.ir;
    factorData[QStringLiteral("turnoverRate")] = result.turnoverRate;

    service->updateFactor(requestedFactorId, factorData);
}

void FactorBacktestController::applyPersistedResult(const QVariantMap& result)
{
    m_backtestResult = result;
    m_groupResults = result.value(QStringLiteral("groups")).toList();
    m_icirResult = result.value(QStringLiteral("icirResult")).toMap();
    m_summaryStats = result.value(QStringLiteral("summary")).toMap();
    emit backtestResultChanged(m_backtestResult);
    emit groupResultsChanged(m_groupResults);
    emit icirResultChanged(m_icirResult);
    emit summaryStatsChanged(m_summaryStats);
}

bool FactorBacktestController::persistLatestResult() const
{
    if (m_backtestResult.isEmpty()) {
        return clearPersistedResult();
    }
    QSaveFile file(persistedResultFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromVariant(m_backtestResult);
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0) {
        return false;
    }
    return file.commit();
}

bool FactorBacktestController::clearPersistedResult() const
{
    return !QFile::exists(persistedResultFilePath()) || QFile::remove(persistedResultFilePath());
}

QString FactorBacktestController::persistedResultFilePath() const
{
    return persistedResultFilePathForController();
}

void FactorBacktestController::shutdownBacktestInfrastructure()
{
    if (m_progressTimer) {
        m_progressTimer->stop();
    }
    m_executor.reset();
    m_threadPool.reset();
    resetBatchState();
}

void FactorBacktestController::resetResults()
{
    m_backtestResult.clear();
    m_groupResults.clear();
    m_icirResult.clear();
    m_summaryStats.clear();
    emit backtestResultChanged(m_backtestResult);
    emit groupResultsChanged(m_groupResults);
    emit icirResultChanged(m_icirResult);
    emit summaryStatsChanged(m_summaryStats);
}

void FactorBacktestController::cancelBacktest()
{
    finalizeBacktestFailure(removedReason(), true);
}

bool FactorBacktestController::clearBacktestCache()
{
    m_factorSupportMapCache.clear();
    emit factorSupportMapCacheChanged(m_factorSupportMapCache);
    return true;
}

bool FactorBacktestController::saveResultToFile(const QString& filePath) const
{
    if (m_backtestResult.isEmpty()) {
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromVariant(m_backtestResult);
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0) {
        return false;
    }
    return file.commit();
}

bool FactorBacktestController::loadResultFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    applyPersistedResult(document.object().toVariantMap());
    return true;
}
