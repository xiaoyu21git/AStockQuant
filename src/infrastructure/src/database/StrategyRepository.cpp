#include "database/StrategyRepository.h"
#include "../../ui/bridge/include/StrategyLifecycleStatus.h"
#include "strategy/ResolvedStrategyBehaviorVariant.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QVariant>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDebug>
#include <QRandomGenerator>

using domain::backtest::ResolvedStrategyIdentity;
using domain::backtest::StrategyStoredType;

namespace astock {
namespace database {

namespace {

enum class PersistedStrategyStatusCode {
    Unknown = 0,
    Active,
    Inactive,
    Testing,
    Archived,
};
const QString kStrategyIdKey = QStringLiteral("strategyId");
const QString kEngineStrategyIdKey = QStringLiteral("engineStrategyId");
const QString kStrategyCodeKey = QStringLiteral("strategyCode");
const QString kMetadataKey = QStringLiteral("metadata");
const QString kStrategyIdentityKey = QStringLiteral("strategyIdentity");
const QString kRuntimeKey = QStringLiteral("runtime");
const QString kParametersKey = QStringLiteral("parameters");
const QString kVersionKey = QStringLiteral("version");
const QString kAuthorKey = QStringLiteral("author");
const QString kLanguageKey = QStringLiteral("language");
const QString kStatusKey = QStringLiteral("status");
const QString kStatusIndexKey = QStringLiteral("statusIndex");
const QString kCreatedAtKey = QStringLiteral("createdAt");
const QString kUpdatedAtKey = QStringLiteral("updatedAt");
const QString kStrategyNameKey = QStringLiteral("strategyName");
const QString kDescriptionKey = QStringLiteral("description");
const QString kBehaviorKindKey = QStringLiteral("behaviorKind");
const QString kFactorIdsKey = QStringLiteral("factorIds");
const QString kRuleIdsKey = QStringLiteral("ruleIds");
const QString kEnabledKey = QStringLiteral("enabled");
const QString kUuidKey = QStringLiteral("uuid");
const QString kStrategyTypeIndexKeyName = QStringLiteral("strategyTypeIndex");
const QString kStrategyBehaviorKindKeyName = QStringLiteral("strategyBehaviorKind");
const QString kAssetTypeIndexKey = QStringLiteral("assetTypeIndex");
const QString kTimeFrameIndexKey = QStringLiteral("timeFrameIndex");
const QString kRiskLevelIndexKey = QStringLiteral("riskLevelIndex");
const QString kOptimizationMethodKey = QStringLiteral("optimization_method");
const QString kTagsKey = QStringLiteral("tags");
const QString kParametersTextColumnKey = QStringLiteral("parameters_text");

void assignParsedJsonObjectField(QVariantMap& strategy,
                                 const QString& fieldKey,
                                 const QString& jsonText);

QString persistedStatusCodeText(PersistedStrategyStatusCode statusCode)
{
    switch (statusCode) {
    case PersistedStrategyStatusCode::Active:
        return QStringLiteral("ACTIVE");
    case PersistedStrategyStatusCode::Inactive:
        return QStringLiteral("INACTIVE");
    case PersistedStrategyStatusCode::Testing:
        return QStringLiteral("TESTING");
    case PersistedStrategyStatusCode::Archived:
        return QStringLiteral("ARCHIVED");
    case PersistedStrategyStatusCode::Unknown:
    default:
        return {};
    }
}

QString persistedLanguageText(StrategyLanguageCode languageCode)
{
    switch (languageCode) {
    case StrategyLanguageCode::Cpp:
        return QStringLiteral("CPP");
    case StrategyLanguageCode::Julia:
        return QStringLiteral("JULIA");
    case StrategyLanguageCode::R:
        return QStringLiteral("R");
    case StrategyLanguageCode::Python:
    default:
        return QStringLiteral("PYTHON");
    }
}

PersistedStrategyStatusCode persistedStatusCodeFromLifecycle(strategy_view::StrategyLifecycleStatus status)
{
    switch (status) {
    case strategy_view::StrategyLifecycleStatus::Active:
        return PersistedStrategyStatusCode::Active;
    case strategy_view::StrategyLifecycleStatus::Inactive:
        return PersistedStrategyStatusCode::Inactive;
    case strategy_view::StrategyLifecycleStatus::Testing:
        return PersistedStrategyStatusCode::Testing;
    case strategy_view::StrategyLifecycleStatus::Archived:
        return PersistedStrategyStatusCode::Archived;
    case strategy_view::StrategyLifecycleStatus::Draft:
    case strategy_view::StrategyLifecycleStatus::Running:
    case strategy_view::StrategyLifecycleStatus::Paused:
    case strategy_view::StrategyLifecycleStatus::Stopped:
    case strategy_view::StrategyLifecycleStatus::Unknown:
    default:
        return PersistedStrategyStatusCode::Unknown;
    }
}

StrategyLanguageCode persistedLanguageCodeFromRaw(const QString& rawLanguage)
{
    const QString normalized = rawLanguage.trimmed().toUpper();
    if (normalized == persistedLanguageText(StrategyLanguageCode::Cpp)) {
        return StrategyLanguageCode::Cpp;
    }
    if (normalized == persistedLanguageText(StrategyLanguageCode::Julia)) {
        return StrategyLanguageCode::Julia;
    }
    if (normalized == persistedLanguageText(StrategyLanguageCode::R)) {
        return StrategyLanguageCode::R;
    }
    return StrategyLanguageCode::Python;
}

void applyRuntimeIndexesToMap(QVariantMap& target, const StrategyRuntimeProperties& indexes);

void assignStrategyBaseFields(QVariantMap& strategy, const QSqlQuery& query)
{
    strategy.insert(kStrategyIdKey, query.value(kStrategyIdKey));
    strategy.insert(kEngineStrategyIdKey, query.value(kEngineStrategyIdKey));
    strategy.insert(kStrategyCodeKey, query.value(kStrategyCodeKey));
    strategy.insert(kVersionKey, query.value(kVersionKey));
    strategy.insert(kAuthorKey, query.value(kAuthorKey));
    strategy.insert(kLanguageKey, query.value(kLanguageKey));
    strategy.remove(kStatusKey);
    const QString normalizedStatusText = query.value(kStatusKey).toString().trimmed().toUpper();
    strategy_view::StrategyLifecycleStatus status = strategy_view::StrategyLifecycleStatus::Unknown;
    if (normalizedStatusText == persistedStatusCodeText(PersistedStrategyStatusCode::Active)) {
        status = strategy_view::StrategyLifecycleStatus::Active;
    } else if (normalizedStatusText == persistedStatusCodeText(PersistedStrategyStatusCode::Inactive)) {
        status = strategy_view::StrategyLifecycleStatus::Inactive;
    } else if (normalizedStatusText == persistedStatusCodeText(PersistedStrategyStatusCode::Testing)) {
        status = strategy_view::StrategyLifecycleStatus::Testing;
    } else if (normalizedStatusText == persistedStatusCodeText(PersistedStrategyStatusCode::Archived)) {
        status = strategy_view::StrategyLifecycleStatus::Archived;
    }
    if (strategy_view::isKnownStrategyLifecycleStatus(status)) {
        strategy.insert(kStatusIndexKey, strategy_view::strategyLifecycleStatusIndex(status));
    } else {
        strategy.remove(kStatusIndexKey);
    }
    strategy.insert(kCreatedAtKey, query.value(kCreatedAtKey));
    strategy.insert(kUpdatedAtKey, query.value(kUpdatedAtKey));

    assignParsedJsonObjectField(strategy,
                                kMetadataKey,
                                query.value(kMetadataKey).toString());
    assignParsedJsonObjectField(strategy,
                                kStrategyIdentityKey,
                                query.value(kStrategyIdentityKey).toString());
    assignParsedJsonObjectField(strategy,
                                kRuntimeKey,
                                query.value(kRuntimeKey).toString());
}

StrategyRuntimeProperties runtimeIndexesFromMap(const QVariantMap& map)
{
    StrategyRuntimeProperties indexes;
    indexes.assetTypeIndex = map.value(kAssetTypeIndexKey).toInt();
    indexes.timeFrameIndex = map.value(kTimeFrameIndexKey).toInt();
    indexes.riskLevelIndex = map.value(kRiskLevelIndexKey).toInt();
    return indexes;
}

void applyRuntimeIndexesToMap(QVariantMap& target, const StrategyRuntimeProperties& indexes)
{
    if (indexes.assetTypeIndex > 0) {
        target.insert(kAssetTypeIndexKey, indexes.assetTypeIndex);
    } else {
        target.remove(kAssetTypeIndexKey);
    }

    if (indexes.timeFrameIndex > 0) {
        target.insert(kTimeFrameIndexKey, indexes.timeFrameIndex);
    } else {
        target.remove(kTimeFrameIndexKey);
    }

    if (indexes.riskLevelIndex > 0) {
        target.insert(kRiskLevelIndexKey, indexes.riskLevelIndex);
    } else {
        target.remove(kRiskLevelIndexKey);
    }
}

QString persistedStrategyTypeIndexKey()
{
    return QString::fromUtf8(domain::backtest::detail::kStrategyTypeIndexKey);
}



bool tryParseJsonObjectVariantMap(const QString& jsonText,
                                 QVariantMap& parsedMap,
                                 QString* errorMessage = nullptr)
{
    parsedMap.clear();
    if (jsonText.isEmpty() || jsonText == QStringLiteral("{}")) {
        return true;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMessage != nullptr) {
            *errorMessage = parseError.errorString();
        }
        return false;
    }

    if (!doc.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("JSON root is not an object");
        }
        return false;
    }

    parsedMap = doc.object().toVariantMap();
    return true;
}

void assignParsedJsonObjectField(QVariantMap& strategy,
                                 const QString& fieldKey,
                                 const QString& jsonText)
{
    QVariantMap value;
    if (tryParseJsonObjectVariantMap(jsonText, value) && !value.isEmpty()) {
        strategy.insert(fieldKey, value);
    }
}

QString persistedPerformanceMetricsKey()
{
    return QStringLiteral("performanceMetrics");
}

QString persistedStrategyBaseColumnsSql()
{
    return QStringLiteral(
    "strategy_id AS strategyId, engine_strategy_id AS engineStrategyId, strategy_code AS strategyCode, "
    "CAST(metadata_json AS CHAR) AS metadata, "
    "CAST(strategy_identity_json AS CHAR) AS strategyIdentity, "
    "version, author, language, status, created_at AS createdAt, updated_at AS updatedAt, "
    "CAST(runtime_json AS CHAR) AS runtime, "
    "CAST(performance_metrics AS CHAR) AS performanceMetrics");
}

QString selectStrategiesSql()
{
    return QStringLiteral("SELECT %1, CAST(parameters AS CHAR) AS parameters FROM strategy ORDER BY created_at DESC")
        .arg(persistedStrategyBaseColumnsSql());
}

QString selectStrategiesWithoutParametersSql(const QString& suffix)
{
    return QStringLiteral("SELECT %1 FROM strategy %2")
        .arg(persistedStrategyBaseColumnsSql(), suffix);
}

QString persistedStrategyUpdateAssignmentsSql()
{
    return QStringLiteral(
        "strategy_code = ?, "
    "metadata_json = ?, "
    "strategy_identity_json = ?, "
        "version = ?, "
        "author = ?, "
        "language = ?, "
    "status = ?, "
    "runtime_json = ?, "
    "performance_metrics = ?");
}

QString persistedStrategyInsertColumnsSql()
{
    return QStringLiteral(
        "strategy_id, strategy_code, metadata_json, strategy_identity_json, "
        "version, author, language, status, runtime_json, performance_metrics, "
        "created_at, updated_at");
}

QString persistedStrategyInsertValuesSql()
{
    return QStringLiteral("?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NOW(), NOW()");
}

QString toCompactJsonText(const QVariantMap& value)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(value)).toJson(QJsonDocument::Compact));
}

QVariantMap metadataToVariantMap(const domain::strategies::StrategyMetadata& metadata)
{
    QVariantList factorIdList;
    factorIdList.reserve(static_cast<qsizetype>(metadata.factorIds.size()));
    for (const domain::strategies::FactorId value : metadata.factorIds) {
        factorIdList.push_back(QVariant::fromValue<qulonglong>(value));
    }

    QVariantList ruleIdList;
    ruleIdList.reserve(static_cast<qsizetype>(metadata.ruleIds.size()));
    for (const domain::strategies::RuleId value : metadata.ruleIds) {
        ruleIdList.push_back(QVariant::fromValue<qulonglong>(value));
    }

    QVariantMap map;
    map.insert(QStringLiteral("name"), QString::fromStdString(metadata.name));
    map.insert(QStringLiteral("description"), QString::fromStdString(metadata.description));
    map.insert(QStringLiteral("behaviorKind"), static_cast<int>(metadata.behaviorKind));
    map.insert(QStringLiteral("factorIds"), factorIdList);
    map.insert(QStringLiteral("ruleIds"), ruleIdList);
    map.insert(QStringLiteral("enabled"), metadata.enabled);
    if (metadata.uuid.is_valid()) {
        map.insert(QStringLiteral("uuid"), QString::fromStdString(metadata.uuid.to_string()));
    }
    return map;
}

domain::strategies::StrategyMetadata metadataFromVariantMap(const QVariantMap& map)
{
    domain::strategies::StrategyMetadata metadata;
    metadata.name = map.value(QStringLiteral("name")).toString().toStdString();
    metadata.description = map.value(QStringLiteral("description")).toString().toStdString();
    metadata.enabled = map.value(QStringLiteral("enabled"), true).toBool();

    const int behaviorIndex = map.value(QStringLiteral("behaviorKind"), static_cast<int>(domain::strategies::StrategyBehaviorKind::Custom)).toInt();
    if (behaviorIndex >= static_cast<int>(domain::strategies::StrategyBehaviorKind::TrendFollowing)
        && behaviorIndex <= static_cast<int>(domain::strategies::StrategyBehaviorKind::Custom)) {
        metadata.behaviorKind = static_cast<domain::strategies::StrategyBehaviorKind>(behaviorIndex);
    }

    const QString rawUuid = map.value(QStringLiteral("uuid")).toString().trimmed();
    if (!rawUuid.isEmpty() && foundation::utils::Uuid::is_valid_uuid(rawUuid.toStdString())) {
        metadata.uuid = foundation::utils::Uuid::from_string(rawUuid.toStdString());
    }

    const QVariant rawFactorIds = map.value(QStringLiteral("factorIds"));
    if (rawFactorIds.isValid() && !rawFactorIds.isNull()) {
        const QVariantList rawFactorIdList = rawFactorIds.toList();
        std::vector<domain::strategies::FactorId> parsedFactorIds;
        parsedFactorIds.reserve(static_cast<size_t>(rawFactorIdList.size()));
        bool factorIdsValid = true;
        for (const QVariant& item : rawFactorIdList) {
            bool ok = false;
            const qulonglong value = item.toULongLong(&ok);
            if (!ok || value == 0ULL) {
                factorIdsValid = false;
                break;
            }
            parsedFactorIds.push_back(static_cast<domain::strategies::FactorId>(value));
        }
        if (factorIdsValid) {
            metadata.factorIds = std::move(parsedFactorIds);
        }
    }

    const QVariant rawRuleIds = map.value(QStringLiteral("ruleIds"));
    if (rawRuleIds.isValid() && !rawRuleIds.isNull()) {
        const QVariantList rawRuleIdList = rawRuleIds.toList();
        std::vector<domain::strategies::RuleId> parsedRuleIds;
        parsedRuleIds.reserve(static_cast<size_t>(rawRuleIdList.size()));
        bool ruleIdsValid = true;
        for (const QVariant& item : rawRuleIdList) {
            bool ok = false;
            const qulonglong value = item.toULongLong(&ok);
            if (!ok || value == 0ULL) {
                ruleIdsValid = false;
                break;
            }
            parsedRuleIds.push_back(static_cast<domain::strategies::RuleId>(value));
        }
        if (ruleIdsValid) {
            metadata.ruleIds = std::move(parsedRuleIds);
        }
    }

    return metadata;
}

QString persistedStrategyBehaviorKindKey()
{
    return QString::fromUtf8(domain::backtest::detail::kStrategyBehaviorKindKey);
}

QVariantMap buildPersistedParameters(const QVariantMap& strategy)
{
    QVariantMap parameters = strategy.value(kParametersKey).toMap();
    const StrategyRuntimeProperties runtimeIndexes = runtimeIndexesFromMap(strategy);

    static const QStringList kPassthroughKeys = {
        kOptimizationMethodKey,
        persistedPerformanceMetricsKey(),
        kTagsKey
    };
    for (const QString& key : kPassthroughKeys) {
        if (!strategy.contains(key)) {
            continue;
        }

        const QVariant value = strategy.value(key);
        if (!value.isValid() || value.isNull()) {
            continue;
        }

        parameters.insert(key, value);
    }

    applyRuntimeIndexesToMap(parameters, runtimeIndexes);

    return parameters;
}

QVariantMap mergeVariantMapsRecursive(const QVariantMap& base, const QVariantMap& overlay)
{
    QVariantMap merged = base;
    for (auto it = overlay.constBegin(); it != overlay.constEnd(); ++it) {
        const QVariant existingValue = merged.value(it.key());
        if (existingValue.canConvert<QVariantMap>() && it.value().canConvert<QVariantMap>()) {
            merged.insert(it.key(), mergeVariantMapsRecursive(existingValue.toMap(), it.value().toMap()));
            continue;
        }

        merged.insert(it.key(), it.value());
    }
    return merged;
}

}

bool PersistedStrategyData::isValid() const
{
    return !metadata.name.empty() && strategyIdentity.validStoredType;
}

QVariantMap PersistedStrategyData::toVariantMap() const
{
    QVariantMap strategyMap;
    QVariantMap persistedParameters = parameters;
    if (!strategyId.empty()) {
        strategyMap.insert(kStrategyIdKey, QString::fromStdString(strategyId));
    }
    if (engineStrategyId > 0ULL) {
        strategyMap.insert(kEngineStrategyIdKey, QVariant::fromValue<qulonglong>(engineStrategyId));
    }
    if (!strategyCode.empty()) {
        strategyMap.insert(kStrategyCodeKey, QString::fromStdString(strategyCode));
    }
    const QVariantMap metadataMap = metadataToVariantMap(metadata);
    strategyMap.insert(kMetadataKey, metadataMap);
    const QString strategyName = QString::fromStdString(metadata.name);
    if (!strategyName.trimmed().isEmpty()) {
        strategyMap.insert(kStrategyNameKey, strategyName);
    }
    const QString description = QString::fromStdString(metadata.description);
    if (!description.trimmed().isEmpty()) {
        strategyMap.insert(kDescriptionKey, description);
    }
    if (!version.empty()) {
        strategyMap.insert(kVersionKey, QString::fromStdString(version));
    }
    if (!author.empty()) {
        strategyMap.insert(kAuthorKey, QString::fromStdString(author));
    }
    switch (language) {
    case StrategyLanguageCode::Cpp:
        strategyMap.insert(kLanguageKey, persistedLanguageText(StrategyLanguageCode::Cpp));
        break;
    case StrategyLanguageCode::Julia:
        strategyMap.insert(kLanguageKey, persistedLanguageText(StrategyLanguageCode::Julia));
        break;
    case StrategyLanguageCode::R:
        strategyMap.insert(kLanguageKey, persistedLanguageText(StrategyLanguageCode::R));
        break;
    case StrategyLanguageCode::Python:
    default:
        strategyMap.insert(kLanguageKey, persistedLanguageText(StrategyLanguageCode::Python));
        break;
    }

    if (strategy_view::isKnownStrategyLifecycleStatus(status)) {
        strategyMap.insert(kStatusIndexKey, strategy_view::strategyLifecycleStatusIndex(status));
    }

    if (createdAt.isValid()) {
        strategyMap.insert(kCreatedAtKey, createdAt);
    }
    if (updatedAt.isValid()) {
        strategyMap.insert(kUpdatedAtKey, updatedAt);
    }

    if (strategyIdentity.validStoredType) {
        QVariantMap identityMap;
        identityMap.insert(kStrategyTypeIndexKeyName, strategyIdentity.storedTypeIndex());
        if (strategyIdentity.behavior.valid) {
            identityMap.insert(kStrategyBehaviorKindKeyName, strategyIdentity.behavior.index());
        }
        strategyMap.insert(kStrategyIdentityKey, identityMap);
        strategyMap.insert(kStrategyTypeIndexKeyName, strategyIdentity.storedTypeIndex());
        if (strategyIdentity.behavior.valid) {
            strategyMap.insert(kStrategyBehaviorKindKeyName, strategyIdentity.behavior.index());
        } else {
            strategyMap.remove(kStrategyBehaviorKindKeyName);
        }
    }

    applyRuntimeIndexesToMap(persistedParameters, runtime);
    QVariantMap runtimeMap;
    applyRuntimeIndexesToMap(runtimeMap, runtime);
    strategyMap.insert(kRuntimeKey, runtimeMap);
    strategyMap.insert(kParametersKey, persistedParameters);
    if (!performanceMetrics.isEmpty()) {
        strategyMap.insert(persistedPerformanceMetricsKey(), performanceMetrics);
    }

    applyRuntimeIndexesToMap(strategyMap, runtime);
    return strategyMap;
}

PersistedStrategyData PersistedStrategyData::fromVariantMap(const QVariantMap& strategyMap)
{
    PersistedStrategyData data;
    data.strategyId = strategyMap.value(kStrategyIdKey).toString().toStdString();
    data.engineStrategyId = strategyMap.value(kEngineStrategyIdKey).toULongLong();
    data.strategyCode = strategyMap.value(kStrategyCodeKey).toString().toStdString();

    data.metadata = metadataFromVariantMap(strategyMap.value(kMetadataKey).toMap());

    data.strategyIdentity = domain::backtest::resolveStrategyIdentity(
        strategyMap.value(kStrategyIdentityKey).toMap());

    data.version = strategyMap.value(kVersionKey).toString().toStdString();
    data.author = strategyMap.value(kAuthorKey).toString().toStdString();
    data.language = persistedLanguageCodeFromRaw(strategyMap.value(kLanguageKey).toString());
    data.status = strategy_view::resolveStrategyLifecycleStatus(strategyMap.value(kStatusIndexKey));
    data.createdAt = strategyMap.value(kCreatedAtKey).toDateTime();
    data.updatedAt = strategyMap.value(kUpdatedAtKey).toDateTime();
    data.parameters = strategyMap.value(kParametersKey).toMap();
    data.performanceMetrics = strategyMap.value(persistedPerformanceMetricsKey()).toMap();
    data.runtime = runtimeIndexesFromMap(strategyMap.value(kRuntimeKey).toMap());

    return data;
}

StrategyRepository::StrategyRepository() 
    : m_initialized(false) {
}

StrategyRepository::~StrategyRepository() {
}

std::optional<PersistedStrategyData> StrategyRepository::findById(const QString& strategyId) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return std::nullopt;
    }
    
    QSqlQuery query(conn.get());
    query.prepare(selectStrategiesWithoutParametersSql(QStringLiteral("WHERE strategy_id = ?")));
    query.addBindValue(strategyId);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to find strategy by id:" << query.lastError().text();
        return std::nullopt;
    }
    
    if (query.next()) {
        PersistedStrategyData strategyData = rowToStrategyData(query);
        const QVariantMap parameters = loadStrategyParameters(strategyId, conn.get());
        if (parameters.isEmpty()) {
            return strategyData;
        }

        QVariantMap strategyMap = strategyData.toVariantMap();
        strategyMap.insert(kParametersKey, parameters);
        return PersistedStrategyData::fromVariantMap(strategyMap);
        QString strategyId = query.value(kStrategyIdKey).toString();
    }
    
    return std::nullopt;
}

std::optional<PersistedStrategyData> StrategyRepository::findByCode(const QString& strategyCode) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return std::nullopt;
    }
    
    QSqlQuery query(conn.get());
    query.prepare(selectStrategiesWithoutParametersSql(QStringLiteral("WHERE strategy_code = ?")));
    query.addBindValue(strategyCode);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to find strategy by code:" << query.lastError().text();
        return std::nullopt;
    }
    
    if (query.next()) {
        QString strategyId = query.value(kStrategyIdKey).toString();
        PersistedStrategyData strategyData = rowToStrategyData(query);
        const QVariantMap parameters = loadStrategyParameters(strategyId, conn.get());
        if (parameters.isEmpty()) {
            return strategyData;
        }

        QVariantMap strategyMap = strategyData.toVariantMap();
        strategyMap.insert(kParametersKey, parameters);
        return PersistedStrategyData::fromVariantMap(strategyMap);
    }
    
    return std::nullopt;
}

std::vector<PersistedStrategyData> StrategyRepository::findAll() {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return {};
    }

    QSqlDatabase& db = conn.get();

    QSqlQuery query(db);
    query.prepare(selectStrategiesSql());
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to find all strategies:" << query.lastError().text();
        return {};
    }

    std::vector<PersistedStrategyData> strategies;
    while (query.next()) {
        strategies.push_back(rowToStrategyData(query));
    }

    return strategies;
}

std::vector<PersistedStrategyData> StrategyRepository::findByType(StrategyStoredType strategyType) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return {};
    }
    
    if (strategyType == StrategyStoredType::Unknown) {
        return {};
    }

    QSqlQuery query(conn.get());
    query.prepare(selectStrategiesWithoutParametersSql(
        QStringLiteral("WHERE JSON_EXTRACT(strategy_identity_json, '$.%1') = ? ORDER BY created_at DESC")
            .arg(QStringLiteral("strategyTypeIndex"))));
    query.addBindValue(static_cast<int>(strategyType));
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to find strategies by type:" << query.lastError().text();
        return {};
    }
    
    std::vector<PersistedStrategyData> strategies;
    while (query.next()) {
        QString strategyId = query.value(kStrategyIdKey).toString();
        PersistedStrategyData strategyData = rowToStrategyData(query);
        const QVariantMap parameters = loadStrategyParameters(strategyId, conn.get());
        if (!parameters.isEmpty()) {
            QVariantMap strategyMap = strategyData.toVariantMap();
            strategyMap.insert(kParametersKey, parameters);
            strategyData = PersistedStrategyData::fromVariantMap(strategyMap);
        }
        strategies.push_back(std::move(strategyData));
    }
    
    return strategies;
}

std::vector<PersistedStrategyData> StrategyRepository::findByStatus(strategy_view::StrategyLifecycleStatus status) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return {};
    }

    const PersistedStrategyStatusCode statusCode = persistedStatusCodeFromLifecycle(status);
    if (statusCode == PersistedStrategyStatusCode::Unknown) {
        qWarning() << "[StrategyRepository] Unsupported persisted strategy status index"
                   << strategy_view::strategyLifecycleStatusIndex(status);
        return {};
    }
    
    QSqlQuery query(conn.get());
    query.prepare(selectStrategiesWithoutParametersSql(
        QStringLiteral("WHERE status = ? ORDER BY created_at DESC")));
    query.addBindValue(persistedStatusCodeText(statusCode));
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to find strategies by status:" << query.lastError().text();
        return {};
    }
    
    std::vector<PersistedStrategyData> strategies;
    while (query.next()) {
        QString strategyId = query.value(kStrategyIdKey).toString();
        PersistedStrategyData strategyData = rowToStrategyData(query);
        const QVariantMap parameters = loadStrategyParameters(strategyId, conn.get());
        if (!parameters.isEmpty()) {
            QVariantMap strategyMap = strategyData.toVariantMap();
            strategyMap.insert(kParametersKey, parameters);
            strategyData = PersistedStrategyData::fromVariantMap(strategyMap);
        }
        strategies.push_back(std::move(strategyData));
    }
    
    return strategies;
}

std::vector<PersistedStrategyData> StrategyRepository::search(const QString& keyword) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return {};
    }
    
    QSqlQuery query(conn.get());
    QString searchPattern = "%" + keyword + "%";
    query.prepare(selectStrategiesWithoutParametersSql(
        QStringLiteral("WHERE strategy_code LIKE ? OR metadata_json LIKE ? ORDER BY created_at DESC")));
    query.addBindValue(searchPattern);
    query.addBindValue(searchPattern);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to search strategies:" << query.lastError().text();
        return {};
    }
    
    std::vector<PersistedStrategyData> strategies;
    while (query.next()) {
        QString strategyId = query.value(QStringLiteral("strategyId")).toString();
        PersistedStrategyData strategyData = rowToStrategyData(query);
        const QVariantMap parameters = loadStrategyParameters(strategyId, conn.get());
        if (!parameters.isEmpty()) {
            QVariantMap strategyMap = strategyData.toVariantMap();
            strategyMap.insert(QStringLiteral("parameters"), parameters);
            strategyData = PersistedStrategyData::fromVariantMap(strategyMap);
        }
        strategies.push_back(std::move(strategyData));
    }
    
    return strategies;
}

QString StrategyRepository::save(const PersistedStrategyData& strategy) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return QString();
    }
    
    QSqlDatabase& db = conn.get();
    
    // 开始事务
    if (!db.transaction()) {
        qWarning() << "[StrategyRepository] Failed to start transaction";
        return QString();
    }
    
    QString strategyId = saveStrategyInternal(strategy, db, false);
    bool success = !strategyId.isEmpty();
    
    if (success) {
        if (!db.commit()) {
            qWarning() << "[StrategyRepository] Failed to commit transaction";
            strategyId.clear();
            db.rollback();
        }
    } else {
        db.rollback();
    }
    
    return strategyId;
}

bool StrategyRepository::update(const QString& strategyId, const PersistedStrategyData& strategy) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return false;
    }
    
    QSqlDatabase& db = conn.get();
    
    // 检查策略是否存在
    QSqlQuery checkQuery(db);
    checkQuery.prepare("SELECT COUNT(*) FROM strategy WHERE strategy_id = ?");
    checkQuery.addBindValue(strategyId);
    
    if (!checkQuery.exec() || !checkQuery.next() || checkQuery.value(0).toInt() == 0) {
        qWarning() << "[StrategyRepository] Strategy not found for update:" << strategyId;
        return false;
    }
    
    // 开始事务
    if (!db.transaction()) {
        qWarning() << "[StrategyRepository] Failed to start transaction";
        return false;
    }
    
    QSqlQuery existingQuery(db);
    existingQuery.prepare(selectStrategiesWithoutParametersSql(QStringLiteral("WHERE strategy_id = ?")));
    existingQuery.addBindValue(strategyId);

    if (!existingQuery.exec() || !existingQuery.next()) {
        qWarning() << "[StrategyRepository] Failed to load existing strategy for update:" << strategyId
                   << existingQuery.lastError().text();
        return false;
    }

    QVariantMap updatedStrategy = rowToStrategyData(existingQuery).toVariantMap();
    const QVariantMap existingParameters = loadStrategyParameters(strategyId, db);
    if (!existingParameters.isEmpty()) {
        updatedStrategy[kParametersKey] = existingParameters;
    }

    const QVariantMap incomingStrategy = strategy.toVariantMap();

    for (auto it = incomingStrategy.begin(); it != incomingStrategy.end(); ++it) {
        if (it.key() == kParametersKey) {
            updatedStrategy[it.key()] = mergeVariantMapsRecursive(existingParameters, it.value().toMap());
            continue;
        }
        updatedStrategy[it.key()] = it.value();
    }
    updatedStrategy[kStrategyIdKey] = strategyId;

    const PersistedStrategyData mergedStrategy = PersistedStrategyData::fromVariantMap(updatedStrategy);

    QString resultId = saveStrategyInternal(mergedStrategy, db, true);
    bool success = !resultId.isEmpty();
    
    if (success) {
        if (!db.commit()) {
            qWarning() << "[StrategyRepository] Failed to commit transaction";
            success = false;
            db.rollback();
        }
    } else {
        db.rollback();
    }
    
    return success;
}

bool StrategyRepository::remove(const QString& strategyId) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return false;
    }
    
    QSqlDatabase& db = conn.get();
    
    // 开始事务
    if (!db.transaction()) {
        qWarning() << "[StrategyRepository] Failed to start transaction";
        return false;
    }
    
    // 删除参数
    QSqlQuery clearParametersQuery(db);
    clearParametersQuery.prepare("UPDATE strategy SET parameters = '{}' WHERE strategy_id = ?");
    clearParametersQuery.addBindValue(strategyId);
    if (!clearParametersQuery.exec()) {
        qWarning() << "[StrategyRepository] Failed to delete strategy parameters:"
                   << clearParametersQuery.lastError().text();
        db.rollback();
        return false;
    }
    
    // 删除策略
    QSqlQuery query(db);
    query.prepare("DELETE FROM strategy WHERE strategy_id = ?");
    query.addBindValue(strategyId);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to delete strategy:" << query.lastError().text();
        db.rollback();
        return false;
    }
    
    if (query.numRowsAffected() == 0) {
        qWarning() << "[StrategyRepository] Strategy not found for deletion:" << strategyId;
        db.rollback();
        return false;
    }
    
    if (!db.commit()) {
        qWarning() << "[StrategyRepository] Failed to commit transaction";
        db.rollback();
        return false;
    }
    
    return true;
}

size_t StrategyRepository::count() {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return 0;
    }
    
    QSqlQuery query(conn.get());
    query.prepare("SELECT COUNT(*) FROM strategy");
    
    if (!query.exec() || !query.next()) {
        qWarning() << "[StrategyRepository] Failed to count strategies:" << query.lastError().text();
        return 0;
    }
    
    return query.value(0).toUInt();
}

bool StrategyRepository::exists(const QString& strategyId) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return false;
    }
    
    QSqlQuery query(conn.get());
    query.prepare("SELECT COUNT(*) FROM strategy WHERE strategy_id = ?");
    query.addBindValue(strategyId);
    
    if (!query.exec() || !query.next()) {
        qWarning() << "[StrategyRepository] Failed to check strategy existence:" << query.lastError().text();
        return false;
    }
    
    return query.value(0).toInt() > 0;
}

bool StrategyRepository::existsByCode(const QString& strategyCode) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return false;
    }
    
    QSqlQuery query(conn.get());
    query.prepare("SELECT COUNT(*) FROM strategy WHERE strategy_code = ?");
    query.addBindValue(strategyCode);
    
    if (!query.exec() || !query.next()) {
        qWarning() << "[StrategyRepository] Failed to check strategy existence by code:" << query.lastError().text();
        return false;
    }
    
    return query.value(0).toInt() > 0;
}

bool StrategyRepository::initialize() {
    QMutexLocker locker(&m_initMutex);

    if (m_initialized) {
        return true;
    }

    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection for initialization";
        return false;
    }

    m_initialized = true;
    return true;
}

bool StrategyRepository::clearAll() {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return false;
    }
    
    QSqlDatabase& db = conn.get();
    
    // 开始事务
    if (!db.transaction()) {
        qWarning() << "[StrategyRepository] Failed to start transaction";
        return false;
    }
    
    // 清空策略
    QSqlQuery clearStrategyQuery(db);
    if (!clearStrategyQuery.exec("DELETE FROM strategy")) {
        qWarning() << "[StrategyRepository] Failed to clear strategy table:" << clearStrategyQuery.lastError().text();
        db.rollback();
        return false;
    }
    
    if (!db.commit()) {
        qWarning() << "[StrategyRepository] Failed to commit transaction";
        db.rollback();
        return false;
    }
    
    return true;
}

bool StrategyRepository::updateStatus(const QString& strategyId, strategy_view::StrategyLifecycleStatus status) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return false;
    }

    const PersistedStrategyStatusCode statusCode = persistedStatusCodeFromLifecycle(status);
    if (statusCode == PersistedStrategyStatusCode::Unknown) {
        qWarning() << "[StrategyRepository] Unsupported persisted strategy status index"
                   << strategy_view::strategyLifecycleStatusIndex(status);
        return false;
    }
    
    QSqlQuery query(conn.get());
    query.prepare("UPDATE strategy SET status = ?, updated_at = NOW() WHERE strategy_id = ?");
    query.addBindValue(persistedStatusCodeText(statusCode));
    query.addBindValue(strategyId);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to update strategy status:" << query.lastError().text();
        return false;
    }
    
    return query.numRowsAffected() > 0;
}

bool StrategyRepository::updateParameters(const QString& strategyId, const QVariantMap& parameters) {
    const auto strategy = findById(strategyId);
    if (!strategy.has_value()) {
        return false;
    }

    PersistedStrategyData updatedStrategy = strategy.value();
    updatedStrategy.parameters = mergeVariantMapsRecursive(updatedStrategy.parameters, parameters);
    return update(strategyId, updatedStrategy);
}

bool StrategyRepository::updatePerformance(const QString& strategyId, const QVariantMap& performance) {
    const auto strategy = findById(strategyId);
    if (!strategy.has_value()) {
        return false;
    }

    PersistedStrategyData updatedStrategy = strategy.value();
    updatedStrategy.performanceMetrics = performance;
    return update(strategyId, updatedStrategy);
}

std::vector<PersistedStrategyData> StrategyRepository::findActiveStrategies() {
    return findByStatus(strategy_view::StrategyLifecycleStatus::Active);
}

std::vector<PersistedStrategyData> StrategyRepository::findDraftStrategies() {
    return {};
}

PersistedStrategyData StrategyRepository::rowToStrategyData(const QSqlQuery& query) {
    QVariantMap strategy;

    assignStrategyBaseFields(strategy, query);

    int performanceColumn = query.record().indexOf(persistedPerformanceMetricsKey());
    if (performanceColumn >= 0) {
        assignParsedJsonObjectField(strategy,
                                    persistedPerformanceMetricsKey(),
                                    query.value(performanceColumn).toString());
    }
    
    // 解析参数 JSON
    int parametersColumn = query.record().indexOf(kParametersKey);
    if (parametersColumn >= 0) {
        QString parametersJson = query.value(parametersColumn).toString();
        if (!parametersJson.isEmpty()) {
            QVariantMap parameters;
            if (tryParseJsonObjectVariantMap(parametersJson, parameters)) {
                strategy.insert(kParametersKey, parameters);
            }
        }
    }

    return PersistedStrategyData::fromVariantMap(strategy);
}

QVariantMap StrategyRepository::loadStrategyParameters(const QString& strategyId, QSqlDatabase& db) {
    // 策略参数存储在 strategy 表的 parameters 字段（JSON）。
    // 该方法用于读取并解析参数。
    
    QSqlQuery query(db);
    query.prepare("SELECT CAST(parameters AS CHAR) AS parameters_text FROM strategy WHERE strategy_id = ?");
    query.addBindValue(strategyId);
    
    if (!query.exec() || !query.next()) {
        return QVariantMap();
    }
    
    QString parametersJson = query.value(kParametersTextColumnKey).toString();
    if (parametersJson.isEmpty()) {
        return QVariantMap();
    }

    QVariantMap parameters;
    if (!tryParseJsonObjectVariantMap(parametersJson, parameters)) {
        return QVariantMap();
    }

    return parameters;
}

QString StrategyRepository::saveStrategyInternal(const PersistedStrategyData& strategy, QSqlDatabase& db, bool isUpdate) {
    const QVariantMap strategyMap = strategy.toVariantMap();
    QString strategyId = QString::fromStdString(strategy.strategyId);
    QString strategyCode = QString::fromStdString(strategy.strategyCode);
    const QString strategyName = QString::fromStdString(strategy.metadata.name);
    const QString metadataJson = toCompactJsonText(strategyMap.value(kMetadataKey).toMap());
    const QString strategyIdentityJson = toCompactJsonText(strategyMap.value(kStrategyIdentityKey).toMap());
    const QString runtimeJson = toCompactJsonText(strategyMap.value(kRuntimeKey).toMap());
    const QString performanceJson = toCompactJsonText(strategy.performanceMetrics);
    const QString version = QString::fromStdString(strategy.version);
    const QString author = QString::fromStdString(strategy.author);
    const StrategyLanguageCode languageCode = strategy.language;
    const ResolvedStrategyIdentity strategyIdentity = strategy.strategyIdentity;

    if (strategyName.isEmpty() || !strategyIdentity.validStoredType) {
        qWarning() << "[StrategyRepository] Strategy name and type are required";
        return QString();
    }

    const PersistedStrategyStatusCode statusCode = persistedStatusCodeFromLifecycle(strategy.status);
    if (statusCode == PersistedStrategyStatusCode::Unknown) {
        qWarning() << "[StrategyRepository] Strategy statusIndex is required and must be persistable";
        return QString();
    }
    
    // 生成策略代码（当未提供时）
    if (strategyCode.isEmpty() && !isUpdate) {
        strategyCode = generateStrategyCode(strategy);
    }
    
    QSqlQuery query(db);
    
    if (isUpdate) {
        query.prepare(QStringLiteral("UPDATE strategy SET %1, updated_at = NOW() WHERE strategy_id = ?")
                          .arg(persistedStrategyUpdateAssignmentsSql()));
        
        query.addBindValue(strategyCode);
        query.addBindValue(metadataJson);
        query.addBindValue(strategyIdentityJson);
        query.addBindValue(version);
        query.addBindValue(author);
        query.addBindValue(persistedLanguageText(languageCode));
        query.addBindValue(persistedStatusCodeText(statusCode));
        query.addBindValue(runtimeJson);
        query.addBindValue(performanceJson);
        query.addBindValue(strategyId);
    } else {
        // 检查策略代码是否已存在
        if (existsByCode(strategyCode)) {
            const QString baseCode = strategyCode;
            bool resolved = false;

            for (int attempt = 0; attempt < 5; ++attempt) {
                const QString suffix = QString("_%1%2")
                    .arg(QDateTime::currentDateTimeUtc().toString("sszzz"))
                    .arg(QRandomGenerator::global()->bounded(1000, 10000));
                const int maxBaseLength = 100 - suffix.size();
                const QString candidateCode = baseCode.left(maxBaseLength) + suffix;
                if (!existsByCode(candidateCode)) {
                    strategyCode = candidateCode;
                    resolved = true;
                    qWarning() << "[StrategyRepository] Strategy code collision resolved:" << baseCode << "->" << strategyCode;
                    break;
                }
            }

            if (!resolved) {
                qWarning() << "[StrategyRepository] Strategy code already exists:" << strategyCode;
                return QString();
            }
        }
        
        // 新合同要求 strategyId 使用 UUID
        if (strategyId.isEmpty()) {
            strategyId = QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string());
        }
        
        // 插入包含 strategy_id 的记录
        query.prepare(QStringLiteral("INSERT INTO strategy (%1) VALUES (%2)")
                  .arg(persistedStrategyInsertColumnsSql(),
                       persistedStrategyInsertValuesSql()));
        
        query.addBindValue(strategyId);
        query.addBindValue(strategyCode);
        query.addBindValue(metadataJson);
        query.addBindValue(strategyIdentityJson);
        query.addBindValue(version);
        query.addBindValue(author);
        query.addBindValue(persistedLanguageText(languageCode));
        query.addBindValue(persistedStatusCodeText(statusCode));
        query.addBindValue(runtimeJson);
        query.addBindValue(performanceJson);
    }
    
    if (!query.exec()) {
        QSqlError error = query.lastError();
        qWarning() << "[StrategyRepository] Failed to save strategy. Error:" << error.text() 
                   << " | SQL:" << query.lastQuery() 
                   << " | Bound values:" << query.boundValues();
        return QString();
    }
    
    //strategyId
    if (!isUpdate) {
        qDebug() << "[StrategyRepository] Created strategy with ID:" << strategyId;
    }
    
    const QVariantMap parameters = buildPersistedParameters(strategyMap);
    if (isUpdate) {
        QSqlQuery clearParametersQuery(db);
        clearParametersQuery.prepare("UPDATE strategy SET parameters = '{}' WHERE strategy_id = ?");
        clearParametersQuery.addBindValue(strategyId);
        if (!clearParametersQuery.exec()) {
            qWarning() << "[StrategyRepository] Failed to delete strategy parameters:"
                       << clearParametersQuery.lastError().text();
            return QString();
        }
    }

    if (!parameters.isEmpty()) {
        const QString parametersJson = QString::fromUtf8(
            QJsonDocument(QJsonObject::fromVariantMap(parameters)).toJson(QJsonDocument::Compact));

        QSqlQuery saveParametersQuery(db);
        saveParametersQuery.prepare("UPDATE strategy SET parameters = ? WHERE strategy_id = ?");
        saveParametersQuery.addBindValue(parametersJson);
        saveParametersQuery.addBindValue(strategyId);

        if (!saveParametersQuery.exec()) {
            qWarning() << "[StrategyRepository] Failed to save strategy parameters:"
                       << saveParametersQuery.lastError().text();
            return QString();
        }
    }
    
    return strategyId;
}

QString StrategyRepository::generateStrategyCode(const PersistedStrategyData& strategy) const {
    QString name = QString::fromStdString(strategy.metadata.name);
    const ResolvedStrategyIdentity strategyIdentity = strategy.strategyIdentity;
    
    // 从名称生成简写代码前缀
    QString codePrefix;
    switch (strategyIdentity.storedType) {
    case StrategyStoredType::DOUBLE_MOVING_AVERAGE:
        codePrefix = "DMA_";
        break;
    case StrategyStoredType::TURTLE_BREAKOUT:
        codePrefix = "TBR_";
        break;
    case StrategyStoredType::BOLLINGER_BAND_MEAN_REVERSION:
        codePrefix = "BBM_";
        break;
    case StrategyStoredType::RSI_MEAN_REVERSION:
        codePrefix = "RSI_";
        break;
    case StrategyStoredType::MULTI_FACTOR_SELECTION:
        codePrefix = "MFS_";
        break;
    case StrategyStoredType::EARNINGS_SURPRISE:
        codePrefix = "ESU_";
        break;
    case StrategyStoredType::STATISTICAL_PAIR_TRADING:
        codePrefix = "SPT_";
        break;
    case StrategyStoredType::RISK_PARITY_ALLOCATION:
        codePrefix = "RPA_";
        break;
    case StrategyStoredType::MACHINE_LEARNING_SELECTION:
        codePrefix = "MLS_";
        break;
    case StrategyStoredType::ORDER_FLOW_IMBALANCE:
        codePrefix = "OFI_";
        break;
    case StrategyStoredType::VOLATILITY_SPREAD:
        codePrefix = "VSP_";
        break;
    case StrategyStoredType::Portfolio:
        codePrefix = "PTF_";
        break;
    case StrategyStoredType::Custom:
    case StrategyStoredType::Unknown:
    default:
        codePrefix = "GEN_";
        break;
    }
    
    // 使用名称前 10 个字符，转大写并替换空格/连字符
    QString namePart = name.left(10).toUpper().replace(" ", "_").replace("-", "_");
    
    // 添加毫秒时间戳和随机后缀，降低高频创建时的碰撞概率
    QString timestamp = QDateTime::currentDateTimeUtc().toString("yyyyMMddHHmmsszzz");
    QString entropy = QString::number(QRandomGenerator::global()->bounded(1000, 10000));
    
    return codePrefix + namePart + "_" + timestamp + entropy;
}

} // namespace database
} // namespace astock

