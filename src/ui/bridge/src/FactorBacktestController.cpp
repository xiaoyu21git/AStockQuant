#include "FactorBacktestController.h"
#include "DataServiceCache.h"
#include "FactorBacktestResultContract.h"
#include "FactorBacktestPreflightUtils.h"
#include "FactorService.h"
#include "DataFetchFieldContractUtils.h"
#include "DatabaseConnectionManager.h"
#include "RiskConfigService.h"

#include <QDate>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <thread>
namespace {

QString removedReason()
{
    return QStringLiteral("因子引擎侧业务代码已删除");
}

QString idleStatusText()
{
    return QStringLiteral("就绪");
}

QString cancelledBacktestReason()
{
    return QStringLiteral("已取消回测");
}

QString defaultBacktestFailureReason()
{
    return QStringLiteral("因子回测执行失败");
}

QString unsupportedBacktestReason()
{
    return QStringLiteral("当前因子未通过回测检查");
}

QString pendingPreflightReason()
{
    return QStringLiteral("当前因子尚未完成回测检查");
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

QString normalizedBenchmarkSymbolText(const QVariant& value)
{
    return value.toString().trimmed().toUpper();
}

QString resolveBenchmarkSymbolFromMap(const QVariantMap& metadata, bool allowGenericKeys);

QString resolveBenchmarkSymbolFromVariant(const QVariant& value, bool allowGenericKeys)
{
    if (!value.isValid() || value.isNull()) {
        return {};
    }

    const QVariantMap nestedMap = value.toMap();
    if (!nestedMap.isEmpty()) {
        return resolveBenchmarkSymbolFromMap(nestedMap, allowGenericKeys);
    }

    return normalizedBenchmarkSymbolText(value);
}

QString resolveBenchmarkSymbolFromMap(const QVariantMap& metadata, bool allowGenericKeys)
{
    static const QStringList directKeys{
        QStringLiteral("benchmarkSymbol"),
        QStringLiteral("benchmark_symbol"),
        QStringLiteral("benchmarkCode"),
        QStringLiteral("benchmark_code"),
        QStringLiteral("indexSymbol"),
        QStringLiteral("index_symbol"),
        QStringLiteral("indexCode"),
        QStringLiteral("index_code")
    };
    for (const QString& key : directKeys) {
        const QString symbol = resolveBenchmarkSymbolFromVariant(metadata.value(key), false);
        if (!symbol.isEmpty()) {
            return symbol;
        }
    }

    static const QStringList nestedKeys{
        QStringLiteral("benchmark"),
        QStringLiteral("benchmarkInfo"),
        QStringLiteral("benchmarkMetadata"),
        QStringLiteral("index"),
        QStringLiteral("indexInfo"),
        QStringLiteral("indexMetadata")
    };
    for (const QString& key : nestedKeys) {
        const QString symbol = resolveBenchmarkSymbolFromVariant(metadata.value(key), true);
        if (!symbol.isEmpty()) {
            return symbol;
        }
    }

    if (allowGenericKeys) {
        static const QStringList genericKeys{
            QStringLiteral("symbol"),
            QStringLiteral("code")
        };
        for (const QString& key : genericKeys) {
            const QString symbol = resolveBenchmarkSymbolFromVariant(metadata.value(key), false);
            if (!symbol.isEmpty()) {
                return symbol;
            }
        }
    }

    return {};
}

QString resolveConfiguredBenchmarkSymbol(const QVariantMap& runtimeParams,
                                        const QVariantMap& datasetBenchmarkMetadata)
{
    const QString defaultBenchmarkSymbol = QStringLiteral("000300.SH");
    const QString datasetBenchmarkSymbol = resolveBenchmarkSymbolFromMap(datasetBenchmarkMetadata, false);
    QString configuredSymbol = risk::config::benchmarkSymbol(
        runtimeParams,
        datasetBenchmarkSymbol.isEmpty() ? defaultBenchmarkSymbol : datasetBenchmarkSymbol).trimmed().toUpper();

    if (configuredSymbol.isEmpty()) {
        return datasetBenchmarkSymbol.isEmpty() ? defaultBenchmarkSymbol : datasetBenchmarkSymbol;
    }

    if (configuredSymbol == defaultBenchmarkSymbol && !datasetBenchmarkSymbol.isEmpty()) {
        return datasetBenchmarkSymbol;
    }

    return configuredSymbol;
}

bool shouldEnableDateParallelism(int batchFactorCount, int workerCount)
{
    return batchFactorCount <= 1 && workerCount > 1;
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

QString resolveInstanceIdFromFactorValue(
    const QVariant& factorId,
    const std::function<QString(const QVariant&)>& overrideForTests)
{
    if (overrideForTests) {
        return overrideForTests(factorId).trimmed();
    }

    const QString directId = factorId.toString().trimmed();
    if (!directId.isEmpty()) {
        return directId;
    }

    const QVariantMap factorMap = factorId.toMap();
    return factorMap.value(QStringLiteral("instanceId"), factorMap.value(QStringLiteral("factorId"))).toString().trimmed();
}

factor::FactorType resolveRuntimeType(const factor::FactorInstanceInfo& info,
                                      const std::shared_ptr<factor::BaseFactor>& factorInstance);

bool configHasCustomExpression(const factor::FactorInstanceInfo& info);

QStringList normalizedRequiredFields(factor::FactorType runtimeType,
                                     const factor::DataRequirements& requirements);

bool configNeutralizationEnabled(const factor::FactorInstanceInfo& info);

QStringList declaredRequiredFieldsFromConfig(const factor::FactorInstanceInfo& info);

QVariantMap buildSupportInfo(const QString& factorId,
                             const QString& instanceId,
                             factor::FactorType runtimeType,
                             const QString& category,
                             const QString& reason,
                             const QStringList& requiredFields,
                             const QStringList& missingFields,
                             factor::SourceTable sourceTable,
                             bool supported);

int uniqueTradeDateCount(const QVariantList& rows);

QSet<QString> collectAvailableFields(const QVariantMap& cacheSnapshot,
                                     const DataServiceCache::DataSetInfo& dataSetInfo,
                                     const QVariantList& rows);

QVariantMap collectFieldDiagnostics(const QVariantMap& cacheSnapshot);

bool fieldHasUsableValues(const QVariantMap& fieldDiagnostics,
                          const QString& field);

struct SupportMapRuntimeSnapshot {
    std::shared_ptr<astock::database::QtMySQLDatabase> database;
    std::shared_ptr<factor::DataAvailabilityChecker> dataChecker;
    std::shared_ptr<factor::FactorInstanceManager> instanceManager;
    QString errorMessage;
};

SupportMapRuntimeSnapshot resolveSupportMapRuntimeSnapshot(
    const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
    const std::shared_ptr<factor::DataAvailabilityChecker>& dataChecker,
    const std::shared_ptr<factor::FactorInstanceManager>& instanceManager,
    bool skipInstanceRefreshForTests)
{
    SupportMapRuntimeSnapshot snapshot;
    snapshot.database = database;
    snapshot.dataChecker = dataChecker;
    snapshot.instanceManager = instanceManager;

    if (snapshot.instanceManager || skipInstanceRefreshForTests) {
        return snapshot;
    }

    auto& dbManager = astock::database::DatabaseConnectionManager::instance();
    if (!dbManager.initialize()) {
        snapshot.errorMessage = QStringLiteral("因子检查运行时初始化失败：数据库连接初始化失败");
        return snapshot;
    }

    snapshot.database = dbManager.getDatabase();
    if (!snapshot.database) {
        snapshot.errorMessage = QStringLiteral("因子检查运行时初始化失败：数据库实例不可用");
        return snapshot;
    }

    if (!snapshot.dataChecker) {
        snapshot.dataChecker = std::make_shared<factor::DataAvailabilityChecker>(snapshot.database);
    }
    snapshot.instanceManager = std::make_shared<factor::FactorInstanceManager>(snapshot.database, snapshot.dataChecker);
    return snapshot;
}

QVariantMap buildSupportMapRuntimeFailure(const QVariantList& factorIds,
                                         const QString& reason)
{
    QVariantMap supportMap;
    const QVariantList normalized = dedupeFactorIds(factorIds);
    for (const QVariant& factorIdValue : normalized) {
        const QString factorId = factorIdValue.toString().trimmed();
        supportMap.insert(factorId, buildSupportInfo(
            factorId,
            {},
            factor::FactorType::UNKNOWN,
            QStringLiteral("instance-create-failed"),
            reason,
            {},
            {},
            factor::SourceTable::UNKNOWN,
            false));
    }
    return supportMap;
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

QString resolveSupportMapInstanceId(const QVariant& factorId,
                                    const std::function<QString(const QVariant&)>& resolveOverride)
{
    if (resolveOverride) {
        return resolveOverride(factorId).trimmed();
    }

    const QString directId = factorId.toString().trimmed();
    if (!directId.isEmpty()) {
        return directId;
    }

    const QVariantMap factorMap = factorId.toMap();
    return factorMap.value(QStringLiteral("instanceId"), factorMap.value(QStringLiteral("factorId"))).toString().trimmed();
}

factor::FactorInstanceInfo supportMapInstanceInfo(
    const QString& resolvedInstanceId,
    const std::function<factor::FactorInstanceInfo(const QString&)>& instanceInfoOverride,
    const std::shared_ptr<factor::FactorInstanceManager>& instanceManager)
{
    if (instanceInfoOverride) {
        return instanceInfoOverride(resolvedInstanceId);
    }
    if (instanceManager) {
        return instanceManager->getInstanceInfo(resolvedInstanceId.toStdString());
    }

    factor::FactorInstanceInfo info;
    info.instanceId = resolvedInstanceId.toStdString();
    return info;
}

std::shared_ptr<factor::BaseFactor> supportMapFactorInstance(
    const QString& resolvedInstanceId,
    const std::function<std::shared_ptr<factor::BaseFactor>(const QString&)>& factorInstanceOverride,
    const std::shared_ptr<factor::FactorInstanceManager>& instanceManager)
{
    if (factorInstanceOverride) {
        return factorInstanceOverride(resolvedInstanceId);
    }
    if (instanceManager) {
        return instanceManager->createIsolatedInstance(resolvedInstanceId.toStdString());
    }
    return nullptr;
}

QVariantMap buildFactorSupportMapSnapshot(
    const QVariantList& factorIds,
    const QString& startDate,
    const QString& endDate,
    const QVariantMap& cacheSnapshot,
    const QString& dataSourceMode,
    int selectedDatasetId,
    const std::shared_ptr<factor::FactorInstanceManager>& instanceManager,
    const QHash<QString, int>& requiredWarmupTradingDaysOverrideForTests,
    const std::function<QString(const QVariant&)>& resolveInstanceIdOverrideForTests,
    const std::function<factor::FactorInstanceInfo(const QString&)>& instanceInfoOverrideForTests,
    const std::function<std::shared_ptr<factor::BaseFactor>(const QString&)>& factorInstanceOverrideForTests)
{
    QVariantMap supportMap;
    const QVariantList normalized = dedupeFactorIds(factorIds);
    const QString sourceMode = normalizedDataSourceMode(dataSourceMode);
    const bool useCacheMode = sourceMode != QStringLiteral("database");
    const bool hasPartialBacktestWindow = startDate.trimmed().isEmpty() != endDate.trimmed().isEmpty();

    DataServiceCache::DataSetInfo dataSetInfo;
    QVariantList dataSetRows;
    bool hasValidDataSet = false;

    if (useCacheMode) {
        auto& cache = DataServiceCache::getInstance();
        cache.initializeCache();
        if (selectedDatasetId > 0) {
            dataSetInfo = cache.getDataSetInfo(selectedDatasetId);
            hasValidDataSet = dataSetInfo.id > 0;
            if (hasValidDataSet && cacheSnapshot.value(QStringLiteral("tradeDateCount")).toInt() <= 0) {
                dataSetRows = cache.getDataSetById(selectedDatasetId);
            }
        }
    }

    const QSet<QString> availableFields = collectAvailableFields(cacheSnapshot, dataSetInfo, dataSetRows);
    const QVariantMap fieldDiagnostics = collectFieldDiagnostics(cacheSnapshot);
    const int availableTradeDateCount = cacheSnapshot.value(QStringLiteral("tradeDateCount")).toInt() > 0
        ? cacheSnapshot.value(QStringLiteral("tradeDateCount")).toInt()
        : uniqueTradeDateCount(dataSetRows);

    for (const QVariant& factorIdValue : normalized) {
        const QString factorId = factorIdValue.toString().trimmed();
        const QString resolvedInstanceId = resolveSupportMapInstanceId(factorIdValue, resolveInstanceIdOverrideForTests);
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

        const factor::FactorInstanceInfo info = supportMapInstanceInfo(
            resolvedInstanceId,
            instanceInfoOverrideForTests,
            instanceManager);
        const std::shared_ptr<factor::BaseFactor> factorInstance = supportMapFactorInstance(
            resolvedInstanceId,
            factorInstanceOverrideForTests,
            instanceManager);

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
                QStringLiteral("开始日期和结束日期必须同时提供，禁止只传一端"),
                {},
                {},
                factor::SourceTable::UNKNOWN,
                false));
            continue;
        }

        factor::DataRequirements requirements = factorInstance->getDataRequirements();
        const factor::BoundaryRules boundaryRules = factorInstance->getBoundaryRules();
        Q_UNUSED(boundaryRules)
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

        if (selectedDatasetId <= 0) {
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
        QStringList emptyValueFields;
        for (const QString& requiredField : requiredFields) {
            if (!availableFields.contains(requiredField)) {
                missingFields.append(requiredField);
                continue;
            }
            if (!fieldHasUsableValues(fieldDiagnostics, requiredField)) {
                emptyValueFields.append(requiredField);
            }
        }
        missingFields = dedupeStringList(missingFields);
        emptyValueFields = dedupeStringList(emptyValueFields);
        if (!missingFields.isEmpty() || !emptyValueFields.isEmpty()) {
            const QStringList unsupportedFields = dedupeStringList(missingFields + emptyValueFields);
            QString reason;
            if (missingFields.isEmpty()) {
                reason = QStringLiteral("缓存集字段存在但无有效非空值: %1")
                    .arg(emptyValueFields.join(QStringLiteral("、")));
            } else if (emptyValueFields.isEmpty()) {
                reason = QStringLiteral("缓存集缺少因子检查所需字段: %1")
                    .arg(missingFields.join(QStringLiteral("、")));
            } else {
                reason = QStringLiteral("缓存集缺少因子检查所需字段: %1；以下字段存在但无有效非空值: %2")
                    .arg(missingFields.join(QStringLiteral("、")))
                    .arg(emptyValueFields.join(QStringLiteral("、")));
            }
            supportMap.insert(factorId, buildSupportInfo(
                factorId,
                resolvedInstanceId,
                runtimeType,
                QStringLiteral("missing-field"),
                reason,
                requiredFields,
                unsupportedFields,
                sourceTable,
                false));
            continue;
        }

        const int requiredWarmupTradingDays = requiredWarmupTradingDaysOverrideForTests.contains(resolvedInstanceId)
            ? std::max(1, requiredWarmupTradingDaysOverrideForTests.value(resolvedInstanceId))
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
            const QString canonicalKey = factor::bridge::runtimeContractFieldName(key);
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

QVariantMap collectFieldDiagnostics(const QVariantMap& cacheSnapshot)
{
    QVariantMap normalizedDiagnostics;
    const QVariantMap rawDiagnostics = cacheSnapshot.value(QStringLiteral("fieldDiagnostics")).toMap();
    for (auto it = rawDiagnostics.constBegin(); it != rawDiagnostics.constEnd(); ++it) {
        const QString rawField = it.key().trimmed();
        if (rawField.isEmpty()) {
            continue;
        }

        const QString canonicalField = factor::bridge::runtimeContractFieldName(rawField);
        normalizedDiagnostics.insert(canonicalField.isEmpty() ? rawField : canonicalField, it.value().toMap());
    }

    return normalizedDiagnostics;
}

bool fieldHasUsableValues(const QVariantMap& fieldDiagnostics,
                          const QString& field)
{
    const QVariantMap diagnostic = fieldDiagnostics.value(field).toMap();
    if (diagnostic.isEmpty()) {
        return true;
    }

    if (diagnostic.contains(QStringLiteral("nonNullCount"))) {
        return diagnostic.value(QStringLiteral("nonNullCount")).toInt() > 0;
    }

    if (diagnostic.contains(QStringLiteral("latestDateNonNullCount"))) {
        return diagnostic.value(QStringLiteral("latestDateNonNullCount")).toInt() > 0;
    }

    return true;
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

QVariantList buildGroupResultList(const factor::BacktestResult& result)
{
    return FactorBacktestResultContract::buildGroupResults(result);
}

QVariantMap buildIcirResultMap(const factor::BacktestResult& result)
{
    return FactorBacktestResultContract::buildIcirResult(result);
}

QVariantMap buildSummaryResultMap(const factor::BacktestResult& result)
{
    return FactorBacktestResultContract::buildSummaryStats(result);
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
    config[QStringLiteral("actualStartDate")] = QString::fromStdString(result.actualStartDate);
    config[QStringLiteral("warmupTrimmedTradingDays")] = result.warmupTrimmedTradingDays;
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
    , m_status(idleStatusText())
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

bool FactorBacktestController::ensureInstanceRuntime()
{
    if (m_instanceManager) {
        return true;
    }

    if (!m_database) {
        auto& dbManager = astock::database::DatabaseConnectionManager::instance();
        if (!dbManager.initialize()) {
            qWarning() << "FactorBacktestController: 数据库连接初始化失败";
            return false;
        }
        m_database = dbManager.getDatabase();
    }

    if (!m_database) {
        qWarning() << "FactorBacktestController: 数据库实例不可用";
        return false;
    }

    if (!m_dataChecker) {
        m_dataChecker = std::make_shared<factor::DataAvailabilityChecker>(m_database);
    }
    if (!m_instanceManager) {
        m_instanceManager = std::make_shared<factor::FactorInstanceManager>(m_database, m_dataChecker);
    }

    return static_cast<bool>(m_instanceManager);
}

void FactorBacktestController::setSelectedFactorIds(const QVariantList& factorIds)
{
    const QVariantList normalized = dedupeFactorIds(factorIds);
    if (m_selectedFactorIds == normalized) {
        return;
    }

    m_selectedFactorIds = normalized;
    emit selectedFactorIdsChanged(m_selectedFactorIds);

    invalidateSupportMapState(true);
}

void FactorBacktestController::setSelectedDatasetId(int datasetId)
{
    if (m_selectedDatasetId == datasetId) {
        return;
    }
    m_selectedDatasetId = datasetId;
    emit selectedDatasetIdChanged(m_selectedDatasetId);
    invalidateSupportMapState(true);
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
    invalidateSupportMapState(true);
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

void FactorBacktestController::invalidateSupportMapState(bool clearPreflightFailures)
{
    ++m_supportMapRequestSeq;
    m_pendingFilterAfterSupportMap = false;

    if (!m_factorSupportMapCache.isEmpty()) {
        m_factorSupportMapCache.clear();
        emit factorSupportMapCacheChanged(m_factorSupportMapCache);
    }

    if (clearPreflightFailures && !m_lastPreflightFailures.isEmpty()) {
        m_lastPreflightFailures.clear();
        emit lastPreflightFailuresChanged(m_lastPreflightFailures);
    }

    if (m_supportMapRequestInFlight) {
        m_supportMapRequestInFlight = false;
        emit supportMapRequestInFlightChanged(false);
    }
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
    const QVariantList normalizedFactorIds = dedupeFactorIds(factorIds);
    const QVariantMap cachedSupportMapSnapshot = m_factorSupportMapCache;
    setSelectedFactorIds(normalizedFactorIds);
    if (normalizedFactorIds.isEmpty()) {
        finalizeBacktestFailure(QStringLiteral("未选择可执行回测的因子"), false);
        return;
    }

    auto supportMapCoversFactors = [&normalizedFactorIds](const QVariantMap& supportMap) {
        return std::all_of(normalizedFactorIds.cbegin(),
                           normalizedFactorIds.cend(),
                           [&supportMap](const QVariant& factorIdValue) {
                               return !supportMap.value(factorIdValue.toString().trimmed()).toMap().isEmpty();
                           });
    };

    QVariantMap supportMap = supportMapCoversFactors(cachedSupportMapSnapshot)
        ? cachedSupportMapSnapshot
        : m_factorSupportMapCache;

    const QVariantMap filtered = filterFactorIdsBySupport(normalizedFactorIds, supportMap);
    const QVariantList supportedFactorIds = filtered.value(QStringLiteral("supportedFactorIds")).toList();
    if (supportedFactorIds.size() != normalizedFactorIds.size()) {
        QVariantList preflightFailures;
        preflightFailures.reserve(normalizedFactorIds.size() - supportedFactorIds.size());
        for (const QVariant& factorIdValue : normalizedFactorIds) {
            const QString factorId = factorIdValue.toString().trimmed();
            const QVariantMap supportInfo = supportMap.value(factorId).toMap();
            if (!supportInfo.isEmpty() && supportInfo.value(QStringLiteral("supported")).toBool()) {
                continue;
            }

            QVariantMap failure = supportInfo.isEmpty()
                ? QVariantMap{
                    {QStringLiteral("factorId"), factorId},
                    {QStringLiteral("instanceId"), factorId},
                    {QStringLiteral("reason"), pendingPreflightReason()},
                    {QStringLiteral("category"), QStringLiteral("unsupported")},
                    {QStringLiteral("missingFields"), QVariantList{}}
                }
                : buildFailureFromSupportInfo(supportInfo);
            if (failure.value(QStringLiteral("reason")).toString().trimmed().isEmpty()) {
                failure[QStringLiteral("reason")] = unsupportedBacktestReason();
            }
            preflightFailures.append(failure);
        }

        if (m_lastPreflightFailures != preflightFailures) {
            m_lastPreflightFailures = preflightFailures;
            emit lastPreflightFailuresChanged(m_lastPreflightFailures);
        }
        finalizeBacktestFailure(QStringLiteral("因子回测预检失败"), false);
        return;
    }

    if (!m_lastPreflightFailures.isEmpty()) {
        m_lastPreflightFailures.clear();
        emit lastPreflightFailuresChanged(m_lastPreflightFailures);
    }

    if (!m_threadPool) {
        const unsigned int hardwareThreads = std::thread::hardware_concurrency();
        const size_t workerCount = hardwareThreads > 0
            ? (std::min)(static_cast<size_t>(hardwareThreads), size_t{4})
            : size_t{4};
        m_threadPool = std::make_shared<foundation::thread::ThreadPoolExecutor>((std::max)(size_t{2}, workerCount));
    }
    if (!m_cacheManager) {
        m_cacheManager = std::make_shared<factor::FactorCacheManager>();
    }

    resetBatchState();
    resetResults();
    m_cancelRequested.store(false);
    m_batchFactorIds = normalizedFactorIds;
    m_batchResultMaps.resize(static_cast<size_t>(normalizedFactorIds.size()));
    m_activeFactorIndex = 0;
    m_pendingGroupText = groupText;
    m_pendingStartDate = startDate;
    m_pendingEndDate = endDate;
    m_isRunning = true;
    m_progress = 0;
    m_status = QStringLiteral("正在提交回测任务");
    emit isRunningChanged(m_isRunning);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    emit backtestStarted(normalizedFactorIds.first().toString().trimmed());

    try {
        m_pendingDataSourceMode = m_dataSourceMode;
        m_pendingDatasetId = m_selectedDatasetId;
        m_pendingDatasetBenchmarkMetadata = m_selectedDatasetBenchmarkMetadata;
        m_pendingStockPoolSymbols = m_selectedStockPoolSymbols;
        m_pendingRuntimeParams = m_backtestRuntimeParams;
        m_pendingBatchFactorCount = normalizedFactorIds.size();
        m_pendingWorkerCount = static_cast<int>(m_threadPool ? m_threadPool->getWorkerCount() : 0);

        m_pendingBacktestTasks.reserve(static_cast<size_t>(normalizedFactorIds.size()));
        for (int index = 0; index < normalizedFactorIds.size(); ++index) {
            const QString requestedFactorId = normalizedFactorIds.at(index).toString().trimmed();
            PendingBacktestTask pendingTask;
            pendingTask.requestedFactorId = requestedFactorId;
            pendingTask.batchIndex = static_cast<size_t>(index);
            m_pendingBacktestTasks.push_back(std::move(pendingTask));
        }

        if (m_pendingBacktestTasks.empty()) {
            finalizeBacktestFailure(QStringLiteral("未创建任何回测任务"), false);
            return;
        }

        launchNextBacktestTask();
        if (!m_isRunning) {
            return;
        }

        m_progressTimer->start();
        pollBacktestProgress();
    } catch (const std::exception& e) {
        finalizeBacktestFailure(QString::fromUtf8(e.what()), false);
    }
}

QVariantMap FactorBacktestController::buildFactorSupportMap(const QVariantList& factorIds,
                                                           const QString& startDate,
                                                           const QString& endDate,
                                                           const QVariantMap& cacheSnapshot)
{
    ensureInstanceRuntime();
    return buildFactorSupportMapSnapshot(
        factorIds,
        startDate,
        endDate,
        cacheSnapshot,
        m_dataSourceMode,
        m_selectedDatasetId,
        m_instanceManager,
        m_requiredWarmupTradingDaysOverrideForTests,
        m_resolveInstanceIdOverrideForTests,
        m_instanceInfoOverrideForTests,
        m_factorInstanceOverrideForTests);
}

void FactorBacktestController::requestFactorSupportMapAsync(const QVariantList& factorIds,
                                                            const QString& startDate,
                                                            const QString& endDate,
                                                            const QVariantMap& cacheSnapshot,
                                                            quint64 requestId)
{
    if (!m_threadPool) {
        const unsigned int hardwareThreads = std::thread::hardware_concurrency();
        const size_t workerCount = hardwareThreads > 0
            ? (std::min)(static_cast<size_t>(hardwareThreads), size_t{4})
            : size_t{4};
        m_threadPool = std::make_shared<foundation::thread::ThreadPoolExecutor>((std::max)(size_t{2}, workerCount));
    }

    if (!m_supportMapRequestInFlight) {
        m_supportMapRequestInFlight = true;
        emit supportMapRequestInFlightChanged(true);
    }

    const std::shared_ptr<astock::database::QtMySQLDatabase> databaseSnapshot = m_database;
    const std::shared_ptr<factor::DataAvailabilityChecker> dataCheckerSnapshot = m_dataChecker;
    const QString dataSourceModeSnapshot = m_dataSourceMode;
    const int selectedDatasetIdSnapshot = m_selectedDatasetId;
    const std::shared_ptr<factor::FactorInstanceManager> instanceManagerSnapshot = m_instanceManager;
    const QHash<QString, int> warmupSnapshot = m_requiredWarmupTradingDaysOverrideForTests;
    const auto resolveInstanceIdOverrideSnapshot = m_resolveInstanceIdOverrideForTests;
    const auto instanceInfoOverrideSnapshot = m_instanceInfoOverrideForTests;
    const auto factorInstanceOverrideSnapshot = m_factorInstanceOverrideForTests;
    const bool skipInstanceRefreshForTestsSnapshot = m_skipInstanceRefreshForTests;
    QPointer<FactorBacktestController> safeController(this);

    m_threadPool->submit([safeController,
                          factorIds,
                          startDate,
                          endDate,
                          cacheSnapshot,
                          requestId,
                          databaseSnapshot,
                          dataCheckerSnapshot,
                          dataSourceModeSnapshot,
                          selectedDatasetIdSnapshot,
                          instanceManagerSnapshot,
                          warmupSnapshot,
                          resolveInstanceIdOverrideSnapshot,
                          instanceInfoOverrideSnapshot,
                          factorInstanceOverrideSnapshot,
                          skipInstanceRefreshForTestsSnapshot]() {
        const SupportMapRuntimeSnapshot runtimeSnapshot = resolveSupportMapRuntimeSnapshot(
            databaseSnapshot,
            dataCheckerSnapshot,
            instanceManagerSnapshot,
            skipInstanceRefreshForTestsSnapshot);
        const QVariantMap supportMap = !runtimeSnapshot.errorMessage.isEmpty()
            ? buildSupportMapRuntimeFailure(factorIds, runtimeSnapshot.errorMessage)
            : buildFactorSupportMapSnapshot(
                factorIds,
                startDate,
                endDate,
                cacheSnapshot,
                dataSourceModeSnapshot,
                selectedDatasetIdSnapshot,
                runtimeSnapshot.instanceManager,
                warmupSnapshot,
                resolveInstanceIdOverrideSnapshot,
                instanceInfoOverrideSnapshot,
                factorInstanceOverrideSnapshot);

        if (!safeController) {
            return;
        }

        QMetaObject::invokeMethod(
            safeController,
            [safeController, requestId, supportMap]() {
                if (!safeController) {
                    return;
                }

                emit safeController->factorSupportMapReady(requestId, supportMap);
            },
            Qt::QueuedConnection);
    });
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
        unsupportedBacktestReason(),
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
        state[QStringLiteral("reason")] = pendingPreflightReason();
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
    if (requestId != m_supportMapRequestSeq || requestId <= m_supportMapAppliedSeq) {
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

    // 单因子历史卡片复用完整回测结果合同，保持 summary / icirResult / groups 为嵌套结构。
    // 禁止在这里扁平化 annualReturn / icValue / informationRatio 等字段，避免历史卡片与主结果页再次漂移。
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
    auto* self = const_cast<FactorBacktestController*>(this);
    if (self->ensureInstanceRuntime() && m_instanceManager) {
        return m_instanceManager->getInstanceInfo(resolvedInstanceId.toStdString());
    }

    factor::FactorInstanceInfo info;
    info.instanceId = resolvedInstanceId.toStdString();
    return info;
}

factor::BacktestConfig FactorBacktestController::buildBacktestConfig(const QString& resolvedInstanceId,
                                                                     const QString& groupText,
                                                                     const QString& startDate,
                                                                     const QString& endDate,
                                                                     const QString& dataSourceMode,
                                                                     int datasetId,
                                                                     const QVariantMap& datasetBenchmarkMetadata,
                                                                     const QVariantList& selectedStockPoolSymbols,
                                                                     const QVariantMap& backtestRuntimeParams,
                                                                     int batchFactorCount,
                                                                     int workerCount) const
{
    const QString normalizedSourceMode = normalizedDataSourceMode(dataSourceMode);
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
    config.datasetId = datasetId;
    config.startDate = trimmedStartDate.toStdString();
    config.endDate = trimmedEndDate.toStdString();
    config.numGroups = parseGroupCount(groupText);

    const QVariantMap runtimeParams = backtestRuntimeParams;
    config.forwardDays = risk::config::forwardDays(runtimeParams, risk::config::kDefaultForwardDays);
    config.rebalanceDays = risk::config::rebalanceDays(runtimeParams, risk::config::kDefaultRebalanceDays);
    config.transactionCost = risk::config::commissionRate(runtimeParams, risk::config::kDefaultCommissionRate);
    config.slippageRate = risk::config::slippageRate(runtimeParams, risk::config::kDefaultSlippageRate);
    config.riskFreeRate = risk::config::riskFreeRate(runtimeParams, risk::config::kDefaultRiskFreeRate);
    config.benchmarkSymbol = resolveConfiguredBenchmarkSymbol(runtimeParams, datasetBenchmarkMetadata).toStdString();
    config.stopLossRate = risk::config::stopLossPercent(runtimeParams, risk::config::kDefaultStopLossPercent);
    config.takeProfitRate = risk::config::takeProfitPercent(runtimeParams, risk::config::kDefaultTakeProfitPercent);
    config.maxDrawdownLimit = risk::config::maxDrawdownLimit(runtimeParams, risk::config::kDefaultMaxDrawdownLimit);
    config.maxDailyLoss = risk::config::maxDailyLoss(runtimeParams, risk::config::kDefaultMaxDailyLoss);
    config.maxPositionPercent = risk::config::maxPositionPercent(runtimeParams, risk::config::kDefaultMaxPositionPercent);
    config.maxTotalExposure = risk::config::maxTotalExposure(runtimeParams, risk::config::kDefaultMaxTotalExposure);
    config.enableDateParallelism = shouldEnableDateParallelism(batchFactorCount, workerCount);

    qDebug() << "FactorBacktestController: 构建回测配置"
             << "instanceId=" << resolvedInstanceId
             << "datasetId=" << datasetId
             << "batchFactorCount=" << batchFactorCount
             << "threadPoolReady=" << (workerCount > 0)
             << "workerCount=" << workerCount
             << "enableDateParallelism=" << config.enableDateParallelism
             << "startDate=" << trimmedStartDate
             << "endDate=" << trimmedEndDate;

    for (const QVariant& symbolValue : normalizedStockPoolSymbols(selectedStockPoolSymbols)) {
        config.allowedStockCodes.push_back(symbolValue.toString().toStdString());
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
    if (!m_isRunning || m_cancelRequested.load() || m_pendingBacktestTasks.empty()) {
        return;
    }

    const auto activeTaskIt = std::find_if(m_pendingBacktestTasks.cbegin(),
                                           m_pendingBacktestTasks.cend(),
                                           [](const PendingBacktestTask& task) {
                                               return task.launchFuture || task.future;
                                           });
    if (activeTaskIt != m_pendingBacktestTasks.cend()) {
        return;
    }

    auto nextTaskIt = std::find_if(m_pendingBacktestTasks.begin(),
                                   m_pendingBacktestTasks.end(),
                                   [](const PendingBacktestTask& task) {
                                       return !task.requestedFactorId.trimmed().isEmpty()
                                           && !task.launchFuture
                                           && !task.future;
                                   });
    if (nextTaskIt == m_pendingBacktestTasks.end()) {
        return;
    }

    if (!m_threadPool || !m_cacheManager) {
        finalizeBacktestFailure(QStringLiteral("因子回测运行时未初始化"), false);
        return;
    }

    const QString requestedFactorId = nextTaskIt->requestedFactorId;
    const QString groupText = m_pendingGroupText;
    const QString startDate = m_pendingStartDate;
    const QString endDate = m_pendingEndDate;
    const QString sourceModeSnapshot = m_pendingDataSourceMode;
    const int datasetIdSnapshot = m_pendingDatasetId;
    const QVariantMap datasetBenchmarkMetadataSnapshot = m_pendingDatasetBenchmarkMetadata;
    const QVariantList stockPoolSymbolsSnapshot = m_pendingStockPoolSymbols;
    const QVariantMap runtimeParamsSnapshot = m_pendingRuntimeParams;
    const int batchFactorCountSnapshot = m_pendingBatchFactorCount;
    const int workerCountSnapshot = m_pendingWorkerCount;
    const std::shared_ptr<astock::database::QtMySQLDatabase> databaseSnapshot = m_database;
    const std::shared_ptr<factor::DataAvailabilityChecker> dataCheckerSnapshot = m_dataChecker;
    const std::shared_ptr<factor::FactorInstanceManager> instanceManagerSnapshot = m_instanceManager;
    const std::shared_ptr<foundation::thread::ThreadPoolExecutor> threadPoolSnapshot = m_threadPool;
    const std::shared_ptr<factor::FactorCacheManager> cacheManagerSnapshot = m_cacheManager;
    const bool skipInstanceRefreshForTestsSnapshot = m_skipInstanceRefreshForTests;
    const auto resolveInstanceIdOverrideSnapshot = m_resolveInstanceIdOverrideForTests;
    if (!nextTaskIt->launchProgressState) {
        nextTaskIt->launchProgressState = std::make_shared<PendingBacktestLaunchProgressState>();
    }
    nextTaskIt->launchProgressState->update(0, QStringLiteral("正在提交回测任务"));
    const std::shared_ptr<PendingBacktestLaunchProgressState> launchProgressStateSnapshot = nextTaskIt->launchProgressState;

    nextTaskIt->launchFuture = std::make_shared<std::future<PendingBacktestLaunchResult>>(
        m_threadPool->submit([this,
                              requestedFactorId,
                              groupText,
                              startDate,
                              endDate,
                              databaseSnapshot,
                              dataCheckerSnapshot,
                              instanceManagerSnapshot,
                              sourceModeSnapshot,
                              datasetIdSnapshot,
                              datasetBenchmarkMetadataSnapshot,
                              stockPoolSymbolsSnapshot,
                              runtimeParamsSnapshot,
                              threadPoolSnapshot,
                              cacheManagerSnapshot,
                              batchFactorCountSnapshot,
                              workerCountSnapshot,
                              skipInstanceRefreshForTestsSnapshot,
                              resolveInstanceIdOverrideSnapshot,
                              launchProgressStateSnapshot]() -> PendingBacktestLaunchResult {
            PendingBacktestLaunchResult launchResult;

            try {
                if (m_cancelRequested.load()) {
                    launchResult.errorMessage = cancelledBacktestReason();
                    return launchResult;
                }

                launchProgressStateSnapshot->update(20, QStringLiteral("正在解析回测实例"));
                launchResult.resolvedInstanceId = resolveInstanceIdFromFactorValue(
                    requestedFactorId,
                    resolveInstanceIdOverrideSnapshot);
                if (launchResult.resolvedInstanceId.isEmpty()) {
                    throw std::runtime_error(QStringLiteral("未找到可执行回测的因子实例").toUtf8().constData());
                }

                qDebug() << "FactorBacktestController: 已切换到后台线程提交回测"
                         << "requestedFactorId=" << requestedFactorId
                         << "threadId=" << reinterpret_cast<quintptr>(QThread::currentThreadId());

                launchProgressStateSnapshot->update(45, QStringLiteral("正在初始化回测运行时"));
                const SupportMapRuntimeSnapshot runtimeSnapshot = resolveSupportMapRuntimeSnapshot(
                    databaseSnapshot,
                    dataCheckerSnapshot,
                    instanceManagerSnapshot,
                    skipInstanceRefreshForTestsSnapshot);
                if (!runtimeSnapshot.errorMessage.isEmpty() || !runtimeSnapshot.instanceManager) {
                    throw std::runtime_error((runtimeSnapshot.errorMessage.isEmpty()
                        ? QStringLiteral("因子回测运行时未初始化")
                        : runtimeSnapshot.errorMessage).toUtf8().constData());
                }

                launchProgressStateSnapshot->update(70, QStringLiteral("正在构建回测配置"));
                factor::BacktestConfig config = buildBacktestConfig(launchResult.resolvedInstanceId,
                                                                    groupText,
                                                                    startDate,
                                                                    endDate,
                                                                    sourceModeSnapshot,
                                                                    datasetIdSnapshot,
                                                                    datasetBenchmarkMetadataSnapshot,
                                                                    stockPoolSymbolsSnapshot,
                                                                    runtimeParamsSnapshot,
                                                                    batchFactorCountSnapshot,
                                                                    workerCountSnapshot);
                if (m_cancelRequested.load()) {
                    launchResult.errorMessage = cancelledBacktestReason();
                    return launchResult;
                }

                launchProgressStateSnapshot->update(90, QStringLiteral("正在启动回测执行器"));
                launchResult.executor = std::make_shared<factor::FactorBacktestExecutor>(
                    runtimeSnapshot.instanceManager,
                    threadPoolSnapshot,
                    cacheManagerSnapshot);
                factor::FactorBacktestExecutor::ExecutionHandle handle = launchResult.executor->executeTrackedAsync(config);
                launchResult.taskId = handle.taskId;
                launchResult.future = std::make_shared<std::future<factor::BacktestResult>>(std::move(handle.future));
                launchProgressStateSnapshot->update(100, QStringLiteral("回测任务已提交，等待执行"));
            } catch (const std::exception& e) {
                launchResult.errorMessage = QString::fromUtf8(e.what()).trimmed();
            }

            return launchResult;
        }));
}

void FactorBacktestController::detachPendingBacktestTasks()
{
    if (m_pendingBacktestTasks.empty()) {
        return;
    }

    m_detachedBacktestTasks.reserve(m_detachedBacktestTasks.size() + m_pendingBacktestTasks.size());
    for (PendingBacktestTask& task : m_pendingBacktestTasks) {
        m_detachedBacktestTasks.push_back(std::move(task));
    }
    m_pendingBacktestTasks.clear();
}

void FactorBacktestController::cleanupDetachedBacktestTasks(bool waitForCompletion)
{
    if (m_detachedBacktestTasks.empty()) {
        return;
    }

    auto taskCompleted = [waitForCompletion](PendingBacktestTask& task) {
        if (task.launchFuture) {
            if (waitForCompletion) {
                task.launchFuture->wait();
            } else if (task.launchFuture->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                return false;
            }
        }

        if (task.future) {
            if (waitForCompletion) {
                task.future->wait();
            } else if (task.future->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                return false;
            }
        }

        return true;
    };

    if (waitForCompletion) {
        for (PendingBacktestTask& task : m_detachedBacktestTasks) {
            taskCompleted(task);
        }
        m_detachedBacktestTasks.clear();
        return;
    }

    auto completedBegin = std::remove_if(m_detachedBacktestTasks.begin(),
                                         m_detachedBacktestTasks.end(),
                                         [&taskCompleted](PendingBacktestTask& task) {
                                             return taskCompleted(task);
                                         });
    m_detachedBacktestTasks.erase(completedBegin, m_detachedBacktestTasks.end());
}

void FactorBacktestController::resetBatchState()
{
    m_cancelRequested.store(true);
    for (const PendingBacktestTask& task : m_pendingBacktestTasks) {
        if (task.future && task.executor) {
            task.executor->cancel(task.taskId);
        }
    }
    if (m_progressTimer) {
        m_progressTimer->stop();
    }
    detachPendingBacktestTasks();
    cleanupDetachedBacktestTasks(false);
    m_batchFactorIds.clear();
    m_batchResultMaps.clear();
    m_pendingGroupText.clear();
    m_pendingStartDate.clear();
    m_pendingEndDate.clear();
    m_pendingDataSourceMode.clear();
    m_pendingDatasetId = -1;
    m_pendingDatasetBenchmarkMetadata.clear();
    m_pendingStockPoolSymbols.clear();
    m_pendingRuntimeParams.clear();
    m_pendingBatchFactorCount = 0;
    m_pendingWorkerCount = 0;
    m_activeFactorIndex = 0;
}

void FactorBacktestController::pollBacktestProgress()
{
    cleanupDetachedBacktestTasks(false);

    if (!m_isRunning || m_pendingBacktestTasks.empty()) {
        if (m_progressTimer) {
            m_progressTimer->stop();
        }
        return;
    }

    const bool hasActiveTask = std::any_of(m_pendingBacktestTasks.cbegin(),
                                           m_pendingBacktestTasks.cend(),
                                           [](const PendingBacktestTask& task) {
                                               return task.launchFuture || task.future;
                                           });
    if (!hasActiveTask) {
        launchNextBacktestTask();
    }

    for (auto it = m_pendingBacktestTasks.begin(); it != m_pendingBacktestTasks.end();) {
        if (!it->future) {
            if (!it->launchFuture) {
                ++it;
                continue;
            }

            if (it->launchFuture->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                ++it;
                continue;
            }

            PendingBacktestLaunchResult launchResult = it->launchFuture->get();
            it->launchFuture.reset();
            if (!launchResult.errorMessage.isEmpty() || !launchResult.future) {
                finalizeBacktestFailure(launchResult.errorMessage.isEmpty()
                                            ? QStringLiteral("因子回测任务提交失败")
                                            : launchResult.errorMessage,
                                        false);
                return;
            }

            it->resolvedInstanceId = launchResult.resolvedInstanceId;
            it->taskId = launchResult.taskId;
            it->executor = launchResult.executor;
            it->future = std::move(launchResult.future);
            m_status = QStringLiteral("正在回测");
            emit statusChanged(m_status);
            ++it;
            continue;
        }

        if (!it->future || it->future->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }

        factor::BacktestResult result = it->future->get();
        const QString requestedFactorId = it->requestedFactorId;
        const size_t batchIndex = it->batchIndex;
        it = m_pendingBacktestTasks.erase(it);

        if (result.status != "SUCCESS") {
            const QString errorText = QString::fromStdString(result.errorMessage).trimmed();
            finalizeBacktestFailure(errorText.isEmpty() ? QStringLiteral("因子回测执行失败") : errorText, false);
            return;
        }

        finalizeBacktestSuccess(requestedFactorId, result, batchIndex);
        if (!m_isRunning || m_pendingBacktestTasks.empty()) {
            return;
        }

        launchNextBacktestTask();
        return;
    }

    const int totalFactors = (std::max)(1, static_cast<int>(m_batchFactorIds.size()));
    const int completedFactors = static_cast<int>(std::count_if(
        m_batchResultMaps.cbegin(),
        m_batchResultMaps.cend(),
        [](const QVariantMap& item) {
            return !item.isEmpty();
        }));
    double activeTaskProgress = 0.0;
    QString activeStatus = m_status.trimmed().isEmpty() ? QStringLiteral("正在回测") : m_status.trimmed();

    auto activeTaskIt = std::find_if(m_pendingBacktestTasks.cbegin(),
                                     m_pendingBacktestTasks.cend(),
                                     [](const PendingBacktestTask& task) {
                                         return task.launchFuture || task.future;
                                     });
    if (activeTaskIt != m_pendingBacktestTasks.cend()) {
        if (activeTaskIt->future && activeTaskIt->executor) {
            const auto progress = activeTaskIt->executor->getProgress(activeTaskIt->taskId);
            if (progress.status != "NOT_FOUND") {
                activeTaskProgress = static_cast<double>((std::max)(0, (std::min)(100, progress.progress))) / 100.0;
                const QString progressStep = QString::fromStdString(progress.currentStep).trimmed();
                if (!progressStep.isEmpty()) {
                    activeStatus = progressStep;
                }
            }
        } else if (activeTaskIt->launchProgressState) {
            activeTaskProgress = static_cast<double>(activeTaskIt->launchProgressState->value()) / 100.0;
            const QString progressStep = activeTaskIt->launchProgressState->stepText().trimmed();
            if (!progressStep.isEmpty()) {
                activeStatus = progressStep;
            }
        }
    }

    const double aggregateProgress = (static_cast<double>(completedFactors) + activeTaskProgress)
        / static_cast<double>(totalFactors);
    const int percent = (std::max)(0, (std::min)(100, static_cast<int>(std::floor(aggregateProgress * 100.0))));
    const int currentFactorNumber = completedFactors + ((activeTaskIt != m_pendingBacktestTasks.cend()) ? 1 : 0);
    m_activeFactorIndex = completedFactors;
    if (m_progress != percent) {
        m_progress = percent;
        emit progressChanged(m_progress);
    }
    if (m_status != activeStatus) {
        m_status = activeStatus;
        emit statusChanged(m_status);
    }
    emit backtestProgress(m_progress, m_status);
    emit backtestProgressDetailed(m_progress,
                                  m_status,
                                  (std::min)(totalFactors, currentFactorNumber),
                                  totalFactors);

    if (m_pendingBacktestTasks.empty() && m_progressTimer) {
        m_progressTimer->stop();
    }

    cleanupDetachedBacktestTasks(false);
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

    m_activeFactorIndex = static_cast<int>(std::count_if(
        m_batchResultMaps.cbegin(),
        m_batchResultMaps.cend(),
        [](const QVariantMap& item) {
            return !item.isEmpty();
        }));
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
    m_pendingBacktestTasks.clear();
    cleanupDetachedBacktestTasks(false);
    if (m_progressTimer) {
        m_progressTimer->stop();
    }
    persistLatestResult();
    emit backtestCompleted(aggregate);
}

void FactorBacktestController::finalizeBacktestFailure(const QString& errorMessage,
                                                       bool cancelled)
{
    m_cancelRequested.store(true);
    for (const PendingBacktestTask& task : m_pendingBacktestTasks) {
        if (task.future && task.executor) {
            task.executor->cancel(task.taskId);
        }
    }
    detachPendingBacktestTasks();
    cleanupDetachedBacktestTasks(false);
    if (m_progressTimer) {
        m_progressTimer->stop();
    }
    m_isRunning = false;
    m_progress = 0;
    m_status = errorMessage.trimmed().isEmpty()
        ? (cancelled ? cancelledBacktestReason() : defaultBacktestFailureReason())
        : errorMessage.trimmed();
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
    resetBatchState();
    cleanupDetachedBacktestTasks(true);
    m_threadPool.reset();
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
    m_cancelRequested.store(true);
    for (const PendingBacktestTask& task : m_pendingBacktestTasks) {
        if (task.future && task.executor) {
            task.executor->cancel(task.taskId);
        }
    }
    finalizeBacktestFailure(cancelledBacktestReason(), true);
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
