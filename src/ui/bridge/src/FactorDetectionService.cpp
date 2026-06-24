#include "FactorDetectionService.h"
#include "factor_check/FactorSupportCheckCore.h"
#include "factor_check/FactorDetectionCoreService.h"
#include "factor_check/FactorSupportScopeCacheCore.h"
#include "factor_check/FactorSupportScopeKeyCore.h"

#include "AppStoragePaths.h"
#include "DataFetchFieldContractUtils.h"
#include "DatabaseConnectionManager.h"
#include "DataCacheAdapter.h"
#include "QtSqlDatabaseAdapter.h"
#include "foundation/thread/ThreadPoolExecutor.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <algorithm>
#include <future>
#include <thread>
#include <unordered_set>

namespace {

QString toRunFailureToken(const factor::bridge::check::RunFailureCode code)
{
    switch (code) {
    case factor::bridge::check::RunFailureCode::InvalidRunSpec:
        return QStringLiteral("InvalidRunSpec");
    case factor::bridge::check::RunFailureCode::MissingExecutionModule:
        return QStringLiteral("MissingExecutionModule");
    case factor::bridge::check::RunFailureCode::MissingResolvedSymbols:
        return QStringLiteral("MissingResolvedSymbols");
    case factor::bridge::check::RunFailureCode::MissingSelectedFactors:
        return QStringLiteral("MissingSelectedFactors");
    case factor::bridge::check::RunFailureCode::None:
        return QString();
    }

    return QString();
}

std::unordered_set<std::string> toStdStringSet(const QSet<QString>& source)
{
    std::unordered_set<std::string> out;
    out.reserve(static_cast<size_t>(source.size()));
    for (const QString& value : source) {
        const QString normalized = value.trimmed().toLower();
        if (!normalized.isEmpty()) {
            out.insert(normalized.toStdString());
        }
    }
    return out;
}

bool diagnosticHasUsableValues(const QVariantMap& fieldDiagnostics,
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

std::unordered_set<std::string> toUnusableFieldSet(const QVariantMap& fieldDiagnostics)
{
    std::unordered_set<std::string> out;
    out.reserve(static_cast<size_t>(fieldDiagnostics.size()));
    for (auto it = fieldDiagnostics.constBegin(); it != fieldDiagnostics.constEnd(); ++it) {
        const QString field = it.key().trimmed().toLower();
        if (field.isEmpty()) {
            continue;
        }
        if (!diagnosticHasUsableValues(fieldDiagnostics, field)) {
            out.insert(field.toStdString());
        }
    }
    return out;
}

QStringList toQStringList(const std::vector<std::string>& values)
{
    QStringList out;
    out.reserve(static_cast<int>(values.size()));
    for (const std::string& value : values) {
        out.append(QString::fromStdString(value));
    }
    return out;
}

std::string toStdString(const QString& value)
{
    return value.toStdString();
}

std::vector<factor::bridge::check::FieldKey> toFieldKeys(const QStringList& fields)
{
    std::vector<factor::bridge::check::FieldKey> result;
    result.reserve(static_cast<size_t>(fields.size()));
    for (const QString& field : fields) {
        const QString normalized = field.trimmed();
        if (!normalized.isEmpty()) {
            result.push_back(factor::bridge::check::FieldKey{normalized.toStdString()});
        }
    }
    return result;
}

QStringList fromFieldKeys(const std::vector<factor::bridge::check::FieldKey>& fields)
{
    QStringList result;
    result.reserve(static_cast<int>(fields.size()));
    for (const auto& field : fields) {
        result.append(QString::fromStdString(field.value));
    }
    return result;
}

std::string toCompactJsonString(const QVariantMap& map)
{
    return QJsonDocument::fromVariant(map).toJson(QJsonDocument::Compact).toStdString();
}

QVariantMap fromJsonStringToVariantMap(const std::string& json)
{
    if (json.empty()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }

    return document.object().toVariantMap();
}

} // namespace

FactorDetectionService::RuntimeContext FactorDetectionService::resolveRuntimeContext(
    const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
    const std::shared_ptr<factor::DataAvailabilityChecker>& dataChecker,
    const std::shared_ptr<factor::FactorInstanceManager>& instanceManager,
    bool skipInstanceRefreshForTests) const
{
    RuntimeContext context;
    context.database = database;
    context.dataChecker = dataChecker;
    context.instanceManager = instanceManager;

    if (context.instanceManager || skipInstanceRefreshForTests) {
        return context;
    }

    auto& dbManager = astock::database::DatabaseConnectionManager::instance();
    if (!dbManager.initialize()) {
        context.errorMessage = QStringLiteral("因子检查运行时初始化失败：数据库连接初始化失败");
        return context;
    }

    context.database = dbManager.getDatabase();
    if (!context.database) {
        context.errorMessage = QStringLiteral("因子检查运行时初始化失败：数据库实例不可用");
        return context;
    }

    if (!context.dataChecker) {
        context.dataChecker = std::make_shared<factor::DataAvailabilityChecker>(
            std::make_shared<astock::database::QtSqlDatabaseAdapter>(context.database));
    }
    context.instanceManager = std::make_shared<factor::FactorInstanceManager>(
        std::make_shared<astock::database::QtSqlDatabaseAdapter>(context.database), context.dataChecker);
    return context;
}

FactorDetectionService::DetectionResult FactorDetectionService::buildSupportMap(
    const Request& request,
    const RuntimeContext& runtimeContext,
    const Overrides& overrides) const
{
    DetectionResult result;
    result.scopeKey = buildScopeKey(request);
    if (!runtimeContext.errorMessage.isEmpty()) {
        result.supportMap = buildRuntimeFailureSupportMap(request.factorIds, runtimeContext.errorMessage);
        return result;
    }

    const QStringList normalizedFactorIds = dedupeFactorIds(request.factorIds);
    const QString cacheFilePath = overrides.cacheFilePathOverrideForTests
        ? overrides.cacheFilePathOverrideForTests().trimmed()
        : persistentCacheFilePath(request.dataSourceMode, request.selectedDatasetId);
    const factor::bridge::check::PersistedFactorEntryMap persistedScopeEntries =
        loadScopeEntries(cacheFilePath, result.scopeKey);

    QVariantMap supportMap;
    QStringList pendingFactorIds;
    QHash<QString, QString> definitionFingerprints;

    for (const QString& factorIdValue : normalizedFactorIds) {
        const QString factorId = factorIdValue.trimmed();
        const QString resolvedInstanceId = resolveInstanceId(factorIdValue, overrides.resolveInstanceIdOverrideForTests);
        if (factorId.isEmpty() || resolvedInstanceId.isEmpty()) {
            pendingFactorIds.append(factorIdValue);
            continue;
        }

        const factor::FactorInstanceInfo info = resolveInstanceInfo(
            resolvedInstanceId,
            overrides.instanceInfoOverrideForTests,
            runtimeContext.instanceManager);
        const QString definitionFingerprint = buildDefinitionFingerprint(info);
        if (definitionFingerprint.isEmpty()) {
            pendingFactorIds.append(factorIdValue);
            continue;
        }

        definitionFingerprints.insert(factorId, definitionFingerprint);
        const std::string factorKey = factorId.toStdString();
        const auto persistedIt = persistedScopeEntries.find(factorKey);
        if (persistedIt != persistedScopeEntries.end()
            && persistedIt->second.definitionFingerprint == definitionFingerprint.toStdString()
            && !persistedIt->second.supportInfoJson.empty()) {
            const QVariantMap persistedSupportInfo =
                fromJsonStringToVariantMap(persistedIt->second.supportInfoJson);
            if (!persistedSupportInfo.isEmpty()) {
                supportMap.insert(factorId, persistedSupportInfo);
                continue;
            }
        }

        pendingFactorIds.append(factorIdValue);
    }

    if (!pendingFactorIds.isEmpty()) {
        const QVariantMap pendingSupportMap = detectPendingFactors(
            pendingFactorIds,
            request,
            runtimeContext,
            overrides);
        for (auto it = pendingSupportMap.begin(); it != pendingSupportMap.end(); ++it) {
            supportMap.insert(it.key(), it.value());
        }
    }

    if (!cacheFilePath.isEmpty()) {
        // 从已有持久化条目拷贝起点 — persistScopeEntries 是 scope 级别全量覆盖，不能只传增量
        factor::bridge::check::PersistedFactorEntryMap updatedScopeEntries = persistedScopeEntries;
        const std::string checkedAt = toStdString(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        for (const QString& factorIdValue : normalizedFactorIds) {
            const QString factorId = factorIdValue.trimmed();
            const QVariantMap supportInfo = supportMap.value(factorId).toMap();
            if (factorId.isEmpty() || supportInfo.isEmpty()) {
                continue;
            }

            const QString definitionFingerprint = definitionFingerprints.value(factorId);
            if (definitionFingerprint.isEmpty()) {
                continue; // 第一轮就解析失败的因子无法持久化，跳过
            }

            updatedScopeEntries[factorId.toStdString()] = factor::bridge::check::PersistedFactorEntry{
                definitionFingerprint.toStdString(),
                checkedAt,
                toCompactJsonString(supportInfo),
            };
        }

        persistScopeEntries(cacheFilePath, result.scopeKey, updatedScopeEntries);
    }

    result.supportMap = supportMap;
    return result;
}

QString FactorDetectionService::persistentCacheFilePath(const QString& dataSourceMode,
                                                        int selectedDatasetId) const
{
    const QString legacyPath = legacyCacheFilePath();
    if (QFileInfo::exists(legacyPath)) {
        QFile::remove(legacyPath);
    }

    if (normalizedDataSourceMode(dataSourceMode) != QStringLiteral("cache") || selectedDatasetId <= 0) {
        return {};
    }

    return bridge::storage::persistentDatasetFactorSupportPassCacheFilePath(selectedDatasetId);
}

QString FactorDetectionService::normalizedDataSourceMode(const QString& dataSourceMode) const
{
    const QString normalized = dataSourceMode.trimmed().toLower();
    return normalized.isEmpty() ? QStringLiteral("cache") : normalized;
}

QStringList FactorDetectionService::dedupeFactorIds(const QStringList& factorIds) const
{
    QStringList normalized;
    QSet<QString> seen;
    for (const QString& factorIdValue : factorIds) {
        const QString factorId = factorIdValue.trimmed();
        if (factorId.isEmpty() || seen.contains(factorId)) {
            continue;
        }
        seen.insert(factorId);
        normalized.append(factorId);
    }
    return normalized;
}

QStringList FactorDetectionService::dedupeStringList(const QStringList& values) const
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

QVariantList FactorDetectionService::toVariantList(const QStringList& values) const
{
    QVariantList result;
    result.reserve(values.size());
    for (const QString& value : values) {
        result.append(value);
    }
    return result;
}

QDate FactorDetectionService::parseSupportDate(const QVariant& value) const
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

factor::FactorType FactorDetectionService::resolveRuntimeType(
    const factor::FactorInstanceInfo& info,
    const std::shared_ptr<factor::BaseFactor>& factorInstance) const
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

bool FactorDetectionService::configHasCustomExpression(const factor::FactorInstanceInfo& info) const
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

bool FactorDetectionService::configNeutralizationEnabled(const factor::FactorInstanceInfo& info) const
{
    if (!info.config.has("calculation")) {
        return false;
    }
    const auto calculation = info.config.get("calculation");
    return calculation.has("neutralizationEnabled") && calculation.get("neutralizationEnabled").asBool();
}

QStringList FactorDetectionService::declaredRequiredFieldsFromConfig(const factor::FactorInstanceInfo& info) const
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
    fields.reserve(static_cast<int>(required.size()));
    for (size_t index = 0; index < required.size(); ++index) {
        if (!required.at(index).isString()) {
            continue;
        }
        const QString normalized = QString::fromStdString(required.at(index).asString()).trimmed();
        if (!normalized.isEmpty()) {
            fields.append(normalized);
        }
    }

    return dedupeStringList(fields);
}

QStringList FactorDetectionService::normalizedRequiredFields(
    factor::FactorType runtimeType,
    const factor::DataRequirements& requirements) const
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

QVariantMap FactorDetectionService::buildSupportInfo(const factor::bridge::check::SupportInfo& typedInfo) const
{
    const QStringList requiredFields = fromFieldKeys(typedInfo.requiredFields);
    const QStringList missingFields = fromFieldKeys(typedInfo.missingFields);

    QVariantMap info;
    info[QStringLiteral("factorId")] = QString::fromStdString(typedInfo.factorId.value);
    info[QStringLiteral("instanceId")] = QString::fromStdString(typedInfo.instanceId.value);
    info[QStringLiteral("runtimeType")] = static_cast<int>(typedInfo.runtimeType);
    info[QStringLiteral("supported")] = typedInfo.supported;
    info[QStringLiteral("category")] =
        QString::fromStdString(factor::bridge::check::FactorDetectionCoreService::categoryToken(typedInfo.category));
    info[QStringLiteral("reason")] =
        QString::fromStdString(factor::bridge::check::FactorDetectionCoreService::reasonMessage(typedInfo));
    info[QStringLiteral("requiredFields")] = toVariantList(requiredFields);
    info[QStringLiteral("missingFields")] = toVariantList(missingFields);
    info[QStringLiteral("sourceTable")] = static_cast<int>(typedInfo.sourceTable);
    info[QStringLiteral("runFailureReason")] = toRunFailureToken(typedInfo.runFailureCode);
    info[QStringLiteral("runErrorCode")] = toRunFailureToken(typedInfo.runFailureCode);
    return info;
}

QVariantMap FactorDetectionService::collectFieldDiagnostics(const QVariantMap& cacheSnapshot) const
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

bool FactorDetectionService::fieldHasUsableValues(const QVariantMap& fieldDiagnostics,
                                                  const QString& field) const
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

QString FactorDetectionService::buildScopeKey(const Request& request) const
{
    const QByteArray cacheSnapshotBytes =
        QJsonDocument::fromVariant(request.cacheSnapshot).toJson(QJsonDocument::Compact);
    const std::string scopeKey = factor::bridge::check::buildScopeKeyHexMd5(
        normalizedDataSourceMode(request.dataSourceMode).toStdString(),
        request.selectedDatasetId,
        request.startDate.trimmed().toStdString(),
        request.endDate.trimmed().toStdString(),
        cacheSnapshotBytes.toStdString());
    return QString::fromStdString(scopeKey);
}

QString FactorDetectionService::buildDefinitionFingerprint(const factor::FactorInstanceInfo& info) const
{
    return QString::fromStdString(factor::bridge::check::md5Hex(info.toJson().toString()));
}

QString FactorDetectionService::legacyCacheFilePath() const
{
    const QString relativePath = QStringLiteral("factor_support_pass_cache.json");
    const QString targetPath = QDir(bridge::storage::cacheDir()).filePath(relativePath);
    bridge::storage::migrateLegacyFileIfNeeded(targetPath, bridge::storage::legacyLocationsForRelativePath(relativePath));
    bridge::storage::ensureDirectoryExists(QFileInfo(targetPath).dir().absolutePath());
    return targetPath;
}

factor::bridge::check::PersistedFactorEntryMap FactorDetectionService::loadScopeEntries(
    const QString& filePath,
    const QString& scopeKey) const
{
    return factor::bridge::check::loadScopeEntries(
        toStdString(filePath.trimmed()),
        toStdString(scopeKey.trimmed()));
}

void FactorDetectionService::persistScopeEntries(const QString& filePath,
                                                 const QString& scopeKey,
                                                 const factor::bridge::check::PersistedFactorEntryMap& scopeEntries) const
{
    if (filePath.trimmed().isEmpty() || scopeKey.trimmed().isEmpty()) {
        return;
    }

    factor::bridge::check::persistScopeEntries(
        toStdString(filePath.trimmed()),
        toStdString(scopeKey.trimmed()),
        scopeEntries,
        toStdString(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
}

QString FactorDetectionService::resolveInstanceId(
    const QString& factorId,
    const std::function<QString(const QString&)>& resolveOverride) const
{
    if (resolveOverride) {
        return resolveOverride(factorId).trimmed();
    }

    return factorId.trimmed();
}

factor::FactorInstanceInfo FactorDetectionService::resolveInstanceInfo(
    const QString& resolvedInstanceId,
    const std::function<factor::FactorInstanceInfo(const QString&)>& instanceInfoOverride,
    const std::shared_ptr<factor::FactorInstanceManager>& instanceManager) const
{
    if (instanceInfoOverride) {
        return instanceInfoOverride(resolvedInstanceId);
    }

    {
        const std::lock_guard<std::mutex> lock(m_instanceInfoCacheMutex);
        const auto it = m_instanceInfoCache.constFind(resolvedInstanceId);
        if (it != m_instanceInfoCache.constEnd()) {
            return it.value();
        }
    }

    if (instanceManager) {
        const factor::FactorInstanceInfo info = instanceManager->getInstanceInfo(resolvedInstanceId.toStdString());
        const std::lock_guard<std::mutex> lock(m_instanceInfoCacheMutex);
        m_instanceInfoCache.insert(resolvedInstanceId, info);
        return info;
    }

    factor::FactorInstanceInfo info;
    info.instanceId = resolvedInstanceId.toStdString();
    return info;
}

std::shared_ptr<factor::BaseFactor> FactorDetectionService::resolveFactorInstance(
    const QString& resolvedInstanceId,
    const std::function<std::shared_ptr<factor::BaseFactor>(const QString&)>& factorInstanceOverride,
    const std::shared_ptr<factor::FactorInstanceManager>& instanceManager) const
{
    if (factorInstanceOverride) {
        return factorInstanceOverride(resolvedInstanceId);
    }

    {
        const std::lock_guard<std::mutex> lock(m_factorInstanceCacheMutex);
        const auto it = m_factorInstanceCache.constFind(resolvedInstanceId);
        if (it != m_factorInstanceCache.constEnd()) {
            return it.value();
        }
    }

    if (instanceManager) {
        const std::shared_ptr<factor::BaseFactor> instance =
            instanceManager->createIsolatedInstance(resolvedInstanceId.toStdString());
        if (instance) {
            const std::lock_guard<std::mutex> lock(m_factorInstanceCacheMutex);
            m_factorInstanceCache.insert(resolvedInstanceId, instance);
        }
        return instance;
    }
    return nullptr;
}

QVariantMap FactorDetectionService::buildRuntimeFailureSupportMap(const QStringList& factorIds,
                                                                  const QString& reason) const
{
    QVariantMap supportMap;
    const QStringList normalized = dedupeFactorIds(factorIds);
    for (const QString& factorIdValue : normalized) {
        const QString factorId = factorIdValue.trimmed();
        const factor::bridge::check::SupportInfo typedInfo =
            factor::bridge::check::FactorDetectionCoreService::makeRuntimeInitFailure(
                factor::bridge::check::FactorId{factorId.toStdString()},
                reason.toStdString());
        supportMap.insert(factorId, buildSupportInfo(typedInfo));
    }
    return supportMap;
}

QVariantMap FactorDetectionService::detectPendingFactors(
    const QStringList& factorIds,
    const Request& request,
    const RuntimeContext& runtimeContext,
    const Overrides& overrides) const
{
    QVariantMap supportMap;
    const QStringList normalized = dedupeFactorIds(factorIds);

    if (normalized.isEmpty()) {
        return supportMap;
    }

    const QString sourceMode = normalizedDataSourceMode(request.dataSourceMode);
    const bool useCacheMode = sourceMode != QStringLiteral("database");

    QVariantMap dataSetInfo;
    bool hasValidDataSet = false;
    QSet<QString> availableFields;
    QVariantMap fieldDiagnostics;
    int availableTradeDateCount = 0;

    if (useCacheMode) {
        auto& cache = DataCacheAdapter::instance();

        if (request.selectedDatasetId > 0) {
            dataSetInfo = cache.getDataSetInfo(request.selectedDatasetId);
            hasValidDataSet = dataSetInfo.value("id", -1).toInt() > 0;

            if (hasValidDataSet) {
                // 1) 快照 + 元数据 availableFields
                const auto appendSnapshotFields = [&availableFields](const QStringList& values) {
                    for (const QString& value : values) {
                        const QString normalized = value.trimmed();
                        if (!normalized.isEmpty()) availableFields.insert(normalized);
                    }
                };
                appendSnapshotFields(request.cacheSnapshot.value(QStringLiteral("availableFields")).toStringList());
                appendSnapshotFields(dataSetInfo.value("availableFields").toStringList());

                // 2) Arrow schema 字段补全（零数据行加载）— 兜底元数据可能遗漏的字段
                const QStringList schemaFields = cache.getDataSetSchemaFields(request.selectedDatasetId);
                appendSnapshotFields(schemaFields);

                fieldDiagnostics = collectFieldDiagnostics(request.cacheSnapshot);
                availableTradeDateCount = request.cacheSnapshot.value(QStringLiteral("tradeDateCount")).toInt();
                if (availableTradeDateCount <= 0) {
                    availableTradeDateCount = dataSetInfo.value("rowCount", 0).toInt() > 0 ? 1 : 0;
                }
            }
        }
    }

    const bool hasUsableDataSetRows = dataSetInfo.value("rowCount", 0).toInt() > 0;

    SharedContext sharedCtx{
        availableFields,
        toStdStringSet(availableFields),
        toUnusableFieldSet(fieldDiagnostics),
        fieldDiagnostics,
        availableTradeDateCount,
        useCacheMode, hasValidDataSet, hasUsableDataSetRows, dataSetInfo
    };

    if (normalized.size() == 1) {
        return detectSingleFactor(normalized.first(), request, runtimeContext, overrides, sharedCtx);
    }

    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    const size_t maxWorkerCount = hardwareThreads > 0
        ? (std::min)(static_cast<size_t>(hardwareThreads), size_t{4})
        : size_t{4};
    const size_t workerCount = (std::max)(size_t{1}, (std::min)(maxWorkerCount, static_cast<size_t>(normalized.size())));

    foundation::thread::ThreadPoolExecutor executor(workerCount);
    std::vector<std::future<QVariantMap>> futures;
    futures.reserve(normalized.size());
    for (const QString& factorIdValue : normalized) {
        // factorIdValue 按值捕获（循环变量），大对象以 const 引用捕获（生命周期覆盖所有 future）
        futures.push_back(executor.submit([this, factorIdValue, &request, &runtimeContext, &overrides, &sharedCtx]() {
            return detectSingleFactor(factorIdValue, request, runtimeContext, overrides, sharedCtx);
        }));
    }

    for (std::future<QVariantMap>& future : futures) {
        const QVariantMap result = future.get();
        for (auto it = result.constBegin(); it != result.constEnd(); ++it) {
            supportMap.insert(it.key(), it.value());
        }
    }

    return supportMap;
}

QVariantMap FactorDetectionService::detectSingleFactor(
    const QString& factorId,
    const Request& request,
    const RuntimeContext& runtimeContext,
    const Overrides& overrides,
    const SharedContext& sharedContext) const
{
    QVariantMap supportMap;
    const QString normalizedFactorId = factorId.trimmed();
    const QString resolvedInstanceId = resolveInstanceId(normalizedFactorId, overrides.resolveInstanceIdOverrideForTests);
    if (resolvedInstanceId.isEmpty()) {
        const factor::bridge::check::SupportInfo typedInfo =
            factor::bridge::check::FactorDetectionCoreService::makeInstanceMissing(
                factor::bridge::check::FactorId{normalizedFactorId.toStdString()});
        supportMap.insert(normalizedFactorId, buildSupportInfo(typedInfo));
        return supportMap;
    }

    const factor::FactorInstanceInfo info = resolveInstanceInfo(
        resolvedInstanceId,
        overrides.instanceInfoOverrideForTests,
        runtimeContext.instanceManager);
    const std::shared_ptr<factor::BaseFactor> factorInstance = resolveFactorInstance(
        resolvedInstanceId,
        overrides.factorInstanceOverrideForTests,
        runtimeContext.instanceManager);
    const factor::FactorType runtimeType = resolveRuntimeType(info, factorInstance);
    if (!factorInstance) {
        const factor::bridge::check::SupportInfo typedInfo =
            factor::bridge::check::FactorDetectionCoreService::makeInstanceCreateFailed(
                factor::bridge::check::FactorId{normalizedFactorId.toStdString()},
                factor::bridge::check::InstanceId{resolvedInstanceId.toStdString()},
                runtimeType);
        supportMap.insert(normalizedFactorId, buildSupportInfo(typedInfo));
        return supportMap;
    }

    const bool hasPartialBacktestWindow = request.startDate.trimmed().isEmpty() != request.endDate.trimmed().isEmpty();
    if (!sharedContext.useCacheMode && hasPartialBacktestWindow) {
        const factor::bridge::check::SupportInfo typedInfo =
            factor::bridge::check::FactorDetectionCoreService::makeInvalidBacktestWindow(
                factor::bridge::check::FactorId{normalizedFactorId.toStdString()},
                factor::bridge::check::InstanceId{resolvedInstanceId.toStdString()},
                runtimeType);
        supportMap.insert(normalizedFactorId, buildSupportInfo(typedInfo));
        return supportMap;
    }

    factor::DataRequirements requirements = factorInstance->getDataRequirements();
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
            const std::string normalizedField = field.toStdString();
            if (std::find(requirements.requiredFields.begin(), requirements.requiredFields.end(), normalizedField)
                == requirements.requiredFields.end()) {
                requirements.requiredFields.push_back(normalizedField);
            }
        };
        appendRequirementField(factor::bridge::MarketBarFieldKeys::PRE_ADJ_FACTOR);
        appendRequirementField(factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR);
    }
    if (configNeutralizationEnabled(info)) {
        const auto appendRequirementField = [&requirements](const QString& field) {
            const std::string normalizedField = field.toStdString();
            if (std::find(requirements.requiredFields.begin(), requirements.requiredFields.end(), normalizedField)
                == requirements.requiredFields.end()) {
                requirements.requiredFields.push_back(normalizedField);
            }
        };
        appendRequirementField(factor::bridge::MarketBarFieldKeys::INDUSTRY_CODE);
        appendRequirementField(factor::bridge::MarketBarFieldKeys::MARKET_CAP);
    }

    const QStringList requiredFields = normalizedRequiredFields(runtimeType, requirements);
    const factor::SourceTable sourceTable = requirements.sourceTable;
    const int requiredWarmupTradingDays = overrides.requiredWarmupTradingDaysOverrideForTests.contains(resolvedInstanceId)
        ? (std::max)(1, overrides.requiredWarmupTradingDaysOverrideForTests.value(resolvedInstanceId))
        : (std::max)(1, factorInstance->getBoundaryRules().minDataPoints);

    factor::bridge::check::Input input;
    input.useCacheMode = sharedContext.useCacheMode;
    input.hasPartialBacktestWindow = hasPartialBacktestWindow;
    input.customExpressionRequired = (runtimeType == factor::FactorType::CUSTOM);
    input.customExpressionAvailable = configHasCustomExpression(info);
    input.hasValidDataSet = sharedContext.hasValidDataSet;
    input.hasUsableDataSetRows = sharedContext.hasUsableDataSetRows;
    input.selectedDatasetId = request.selectedDatasetId;
    input.availableTradeDateCount = sharedContext.availableTradeDateCount;
    input.requiredWarmupTradingDays = requiredWarmupTradingDays;
    input.availableFields = sharedContext.availableFieldSet;
    input.unusableFields = sharedContext.unusableFieldSet;
    input.requiredFields.reserve(static_cast<size_t>(requiredFields.size()));
    for (const QString& requiredField : requiredFields) {
        input.requiredFields.push_back(requiredField.trimmed().toLower().toStdString());
    }

    const factor::bridge::check::EvaluationResult evaluation =
        factor::bridge::check::evaluateSupport(input);
    const QStringList missingFields = dedupeStringList(toQStringList(evaluation.missingFields));
    const QStringList emptyValueFields = dedupeStringList(toQStringList(evaluation.emptyValueFields));

    const std::vector<factor::bridge::check::FieldKey> requiredFieldKeys = toFieldKeys(requiredFields);
    const std::vector<factor::bridge::check::FieldKey> missingFieldKeys = toFieldKeys(missingFields);
    const std::vector<factor::bridge::check::FieldKey> emptyValueFieldKeys = toFieldKeys(emptyValueFields);

    factor::bridge::check::OutcomeSupportRequest outcomeRequest;
    outcomeRequest.factorId = factor::bridge::check::FactorId{normalizedFactorId.toStdString()};
    outcomeRequest.instanceId = factor::bridge::check::InstanceId{resolvedInstanceId.toStdString()};
    outcomeRequest.runtimeType = runtimeType;
    outcomeRequest.sourceTable = sourceTable;
    outcomeRequest.useCacheMode = sharedContext.useCacheMode;
    outcomeRequest.availableTradeDateCount = sharedContext.availableTradeDateCount;
    outcomeRequest.requiredWarmupTradingDays = requiredWarmupTradingDays;
    outcomeRequest.requiredFields = requiredFieldKeys;
    outcomeRequest.missingFields = missingFieldKeys;
    outcomeRequest.emptyValueFields = emptyValueFieldKeys;
    outcomeRequest.code = evaluation.code;
    outcomeRequest.supported = evaluation.supported;

    factor::bridge::check::SupportInfo typedInfo =
        factor::bridge::check::FactorDetectionCoreService::makeOutcomeBased(outcomeRequest);

    if (evaluation.code == factor::bridge::check::OutcomeCode::MissingOrEmptyFields) {
        typedInfo.missingFields = toFieldKeys(dedupeStringList(missingFields + emptyValueFields));
    }

    supportMap.insert(normalizedFactorId, buildSupportInfo(typedInfo));
    return supportMap;
}
