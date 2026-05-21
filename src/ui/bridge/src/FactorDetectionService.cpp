#include "FactorDetectionService.h"

#include "AppStoragePaths.h"
#include "DataFetchFieldContractUtils.h"
#include "DatabaseConnectionManager.h"
#include "foundation/thread/ThreadPoolExecutor.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <future>
#include <thread>

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
        context.dataChecker = std::make_shared<factor::DataAvailabilityChecker>(context.database);
    }
    context.instanceManager = std::make_shared<factor::FactorInstanceManager>(context.database, context.dataChecker);
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

    const QVariantList normalizedFactorIds = dedupeFactorIds(request.factorIds);
    const QString cacheFilePath = overrides.cacheFilePathOverrideForTests
        ? overrides.cacheFilePathOverrideForTests().trimmed()
        : persistentCacheFilePath(request.dataSourceMode, request.selectedDatasetId);
    const QVariantMap persistedScopeEntries = loadScopeEntries(cacheFilePath, result.scopeKey);

    QVariantMap supportMap;
    QVariantList pendingFactorIds;
    QHash<QString, QString> definitionFingerprints;

    for (const QVariant& factorIdValue : normalizedFactorIds) {
        const QString factorId = factorIdValue.toString().trimmed();
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
        const QVariantMap persistedEntry = persistedScopeEntries.value(factorId).toMap();
        const QVariantMap persistedSupportInfo = persistedEntry.value(QStringLiteral("supportInfo")).toMap();
        if (!persistedSupportInfo.isEmpty()
            && persistedEntry.value(QStringLiteral("definitionFingerprint")).toString() == definitionFingerprint) {
            supportMap.insert(factorId, persistedSupportInfo);
            continue;
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
        QVariantMap updatedScopeEntries = persistedScopeEntries;
        const QString checkedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        for (const QVariant& factorIdValue : normalizedFactorIds) {
            const QString factorId = factorIdValue.toString().trimmed();
            const QVariantMap supportInfo = supportMap.value(factorId).toMap();
            if (factorId.isEmpty() || supportInfo.isEmpty()) {
                continue;
            }

            QString definitionFingerprint = definitionFingerprints.value(factorId);
            if (definitionFingerprint.isEmpty()) {
                const QString resolvedInstanceId = resolveInstanceId(factorIdValue, overrides.resolveInstanceIdOverrideForTests);
                if (!resolvedInstanceId.isEmpty()) {
                    definitionFingerprint = buildDefinitionFingerprint(
                        resolveInstanceInfo(
                            resolvedInstanceId,
                            overrides.instanceInfoOverrideForTests,
                            runtimeContext.instanceManager));
                }
            }
            if (definitionFingerprint.isEmpty()) {
                continue;
            }

            updatedScopeEntries.insert(factorId, QVariantMap{
                {QStringLiteral("definitionFingerprint"), definitionFingerprint},
                {QStringLiteral("checkedAt"), checkedAt},
                {QStringLiteral("supportInfo"), supportInfo}});
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

QVariantList FactorDetectionService::dedupeFactorIds(const QVariantList& factorIds) const
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

QVariantMap FactorDetectionService::buildSupportInfo(const QString& factorId,
                                                     const QString& instanceId,
                                                     factor::FactorType runtimeType,
                                                     const QString& category,
                                                     const QString& reason,
                                                     const QStringList& requiredFields,
                                                     const QStringList& missingFields,
                                                     factor::SourceTable sourceTable,
                                                     bool supported) const
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

int FactorDetectionService::uniqueTradeDateCount(const QVariantList& rows) const
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

QSet<QString> FactorDetectionService::collectAvailableFields(
    const QVariantMap& cacheSnapshot,
    const DataServiceCache::DataSetInfo& dataSetInfo,
    const QVariantList& rows) const
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

QVariantMap FactorDetectionService::normalizedSupportCacheSnapshot(const QVariantMap& cacheSnapshot) const
{
    QVariantMap normalized = cacheSnapshot;
    QStringList availableFields = cacheSnapshot.value(QStringLiteral("availableFields")).toStringList();
    availableFields = dedupeStringList(availableFields);
    std::sort(availableFields.begin(), availableFields.end());
    normalized.insert(QStringLiteral("availableFields"), availableFields);
    return normalized;
}

QString FactorDetectionService::buildScopeKey(const Request& request) const
{
    QVariantMap payload;
    payload.insert(QStringLiteral("dataSourceMode"), normalizedDataSourceMode(request.dataSourceMode));
    payload.insert(QStringLiteral("selectedDatasetId"), request.selectedDatasetId);
    payload.insert(QStringLiteral("startDate"), request.startDate.trimmed());
    payload.insert(QStringLiteral("endDate"), request.endDate.trimmed());
    payload.insert(QStringLiteral("cacheSnapshot"), normalizedSupportCacheSnapshot(request.cacheSnapshot));
    const QByteArray payloadBytes = QJsonDocument::fromVariant(payload).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(payloadBytes, QCryptographicHash::Md5).toHex());
}

QString FactorDetectionService::buildDefinitionFingerprint(const factor::FactorInstanceInfo& info) const
{
    return QString::fromLatin1(QCryptographicHash::hash(
        QByteArray::fromStdString(info.toJson().toString()),
        QCryptographicHash::Md5).toHex());
}

QString FactorDetectionService::legacyCacheFilePath() const
{
    const QString relativePath = QStringLiteral("factor_support_pass_cache.json");
    const QString targetPath = QDir(bridge::storage::cacheDir()).filePath(relativePath);
    bridge::storage::migrateLegacyFileIfNeeded(targetPath, bridge::storage::legacyLocationsForRelativePath(relativePath));
    bridge::storage::ensureDirectoryExists(QFileInfo(targetPath).dir().absolutePath());
    return targetPath;
}

QVariantMap FactorDetectionService::loadCacheRoot(const QString& filePath) const
{
    if (filePath.trimmed().isEmpty()) {
        return {};
    }

    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }

    return document.object().toVariantMap();
}

bool FactorDetectionService::persistCacheRoot(const QString& filePath, const QVariantMap& root) const
{
    if (filePath.trimmed().isEmpty()) {
        return false;
    }

    bridge::storage::ensureDirectoryExists(QFileInfo(filePath).dir().absolutePath());
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromVariant(root);
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0) {
        return false;
    }

    return file.commit();
}

QVariantMap FactorDetectionService::loadScopeEntries(const QString& filePath,
                                                     const QString& scopeKey) const
{
    const QVariantMap root = loadCacheRoot(filePath);
    const QVariantMap scopes = root.value(QStringLiteral("scopes")).toMap();
    const QVariantMap scope = scopes.value(scopeKey).toMap();
    return scope.value(QStringLiteral("factors")).toMap();
}

void FactorDetectionService::persistScopeEntries(const QString& filePath,
                                                 const QString& scopeKey,
                                                 const QVariantMap& scopeEntries) const
{
    if (filePath.trimmed().isEmpty() || scopeKey.trimmed().isEmpty()) {
        return;
    }

    QVariantMap root = loadCacheRoot(filePath);
    QVariantMap scopes = root.value(QStringLiteral("scopes")).toMap();
    QVariantMap scope = scopes.value(scopeKey).toMap();
    scope.insert(QStringLiteral("factors"), scopeEntries);
    scope.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    scopes.insert(scopeKey, scope);
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("scopes"), scopes);
    persistCacheRoot(filePath, root);
}

QString FactorDetectionService::resolveInstanceId(
    const QVariant& factorId,
    const std::function<QString(const QVariant&)>& resolveOverride) const
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

QVariantMap FactorDetectionService::buildRuntimeFailureSupportMap(const QVariantList& factorIds,
                                                                  const QString& reason) const
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

QVariantMap FactorDetectionService::detectPendingFactors(
    const QVariantList& factorIds,
    const Request& request,
    const RuntimeContext& runtimeContext,
    const Overrides& overrides) const
{
    QVariantMap supportMap;
    const QVariantList normalized = dedupeFactorIds(factorIds);

    if (normalized.isEmpty()) {
        return supportMap;
    }

    const QString sourceMode = normalizedDataSourceMode(request.dataSourceMode);
    const bool useCacheMode = sourceMode != QStringLiteral("database");

    DataServiceCache::DataSetInfo dataSetInfo;
    QVariantList dataSetRows;
    bool hasValidDataSet = false;
    QSet<QString> availableFields;
    QVariantMap fieldDiagnostics;
    int availableTradeDateCount = 0;

    if (useCacheMode) {
        auto& cache = DataServiceCache::getInstance();
        cache.initializeCache();

        if (request.selectedDatasetId > 0) {
            dataSetInfo = cache.getDataSetInfo(request.selectedDatasetId);
            hasValidDataSet = dataSetInfo.id > 0;

            if (hasValidDataSet) {
                dataSetRows = cache.getDataSetById(request.selectedDatasetId);
                availableFields = collectAvailableFields(
                    request.cacheSnapshot, dataSetInfo, dataSetRows);
                fieldDiagnostics = collectFieldDiagnostics(request.cacheSnapshot);
                availableTradeDateCount = request.cacheSnapshot.value(QStringLiteral("tradeDateCount")).toInt() > 0
                    ? request.cacheSnapshot.value(QStringLiteral("tradeDateCount")).toInt()
                    : uniqueTradeDateCount(dataSetRows);
            }
        }
    }

    SharedContext sharedCtx{
        availableFields, fieldDiagnostics, availableTradeDateCount,
        useCacheMode, hasValidDataSet, dataSetInfo.rowCount > 0 || !dataSetRows.isEmpty(), dataSetInfo
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
    for (const QVariant& factorIdValue : normalized) {
        futures.push_back(executor.submit([this, factorIdValue, request, runtimeContext, overrides, sharedCtx]() {
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
    const QVariant& factorIdValue,
    const Request& request,
    const RuntimeContext& runtimeContext,
    const Overrides& overrides,
    const SharedContext& sharedContext) const
{
    QVariantMap supportMap;
    const QString factorId = factorIdValue.toString().trimmed();
    const QString resolvedInstanceId = resolveInstanceId(factorIdValue, overrides.resolveInstanceIdOverrideForTests);
    if (resolvedInstanceId.isEmpty()) {
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
        return supportMap;
    }

    const bool hasPartialBacktestWindow = request.startDate.trimmed().isEmpty() != request.endDate.trimmed().isEmpty();
    if (!sharedContext.useCacheMode && hasPartialBacktestWindow) {
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
        return supportMap;
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
        return supportMap;
    }

    if (!sharedContext.useCacheMode) {
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
        return supportMap;
    }

    if (request.selectedDatasetId <= 0) {
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
        return supportMap;
    }

    if (!sharedContext.hasValidDataSet) {
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
        return supportMap;
    }

    if (!sharedContext.hasUsableDataSetRows) {
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
        return supportMap;
    }

    QStringList missingFields;
    QStringList emptyValueFields;
    for (const QString& requiredField : requiredFields) {
        if (!sharedContext.availableFields.contains(requiredField)) {
            missingFields.append(requiredField);
            continue;
        }
        if (!fieldHasUsableValues(sharedContext.fieldDiagnostics, requiredField)) {
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
        return supportMap;
    }

    const int requiredWarmupTradingDays = overrides.requiredWarmupTradingDaysOverrideForTests.contains(resolvedInstanceId)
        ? (std::max)(1, overrides.requiredWarmupTradingDaysOverrideForTests.value(resolvedInstanceId))
        : (std::max)(1, factorInstance->getBoundaryRules().minDataPoints);
    if (sharedContext.availableTradeDateCount > 0 && sharedContext.availableTradeDateCount < requiredWarmupTradingDays) {
        supportMap.insert(factorId, buildSupportInfo(
            factorId,
            resolvedInstanceId,
            runtimeType,
            QStringLiteral("insufficient-history"),
            QStringLiteral("缓存集仅覆盖 %1 个交易日，低于该因子所需的 %2 个交易日")
                .arg(sharedContext.availableTradeDateCount)
                .arg(requiredWarmupTradingDays),
            requiredFields,
            {},
            sourceTable,
            false));
        return supportMap;
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
    return supportMap;
}
