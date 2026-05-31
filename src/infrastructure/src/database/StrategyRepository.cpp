#include "database/StrategyRepository.h"
#include "../../ui/bridge/include/StrategyLifecycleStatus.h"
#include "../../domain/backtest/include/ResolvedStrategyBehaviorVariant.h"
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
#include <algorithm>
#include <ctime>
#include <random>
#include <sstream>
#include <iomanip>

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

QString storedTypeText(StrategyStoredType storedType);

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

PersistedStrategyStatusCode persistedStatusCodeFromDatabase(const QVariant& rawStatus)
{
    const QString normalized = rawStatus.toString().trimmed().toUpper();
    if (normalized == persistedStatusCodeText(PersistedStrategyStatusCode::Active)) {
        return PersistedStrategyStatusCode::Active;
    }
    if (normalized == persistedStatusCodeText(PersistedStrategyStatusCode::Inactive)) {
        return PersistedStrategyStatusCode::Inactive;
    }
    if (normalized == persistedStatusCodeText(PersistedStrategyStatusCode::Testing)) {
        return PersistedStrategyStatusCode::Testing;
    }
    if (normalized == persistedStatusCodeText(PersistedStrategyStatusCode::Archived)) {
        return PersistedStrategyStatusCode::Archived;
    }
    return PersistedStrategyStatusCode::Unknown;
}

strategy_view::StrategyLifecycleStatus lifecycleStatusFromPersistedCode(PersistedStrategyStatusCode statusCode)
{
    switch (statusCode) {
    case PersistedStrategyStatusCode::Active:
        return strategy_view::StrategyLifecycleStatus::Active;
    case PersistedStrategyStatusCode::Inactive:
        return strategy_view::StrategyLifecycleStatus::Inactive;
    case PersistedStrategyStatusCode::Testing:
        return strategy_view::StrategyLifecycleStatus::Testing;
    case PersistedStrategyStatusCode::Archived:
        return strategy_view::StrategyLifecycleStatus::Archived;
    case PersistedStrategyStatusCode::Unknown:
    default:
        return strategy_view::StrategyLifecycleStatus::Unknown;
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

bool bindPersistedStatusCode(QSqlQuery& query, PersistedStrategyStatusCode statusCode)
{
    const QString persistedText = persistedStatusCodeText(statusCode);
    if (persistedText.isEmpty()) {
        return false;
    }

    query.addBindValue(persistedText);
    return true;
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

void bindPersistedLanguageCode(QSqlQuery& query, StrategyLanguageCode languageCode)
{
    query.addBindValue(persistedLanguageText(languageCode));
}

void assignPersistedStrategyIdentity(QVariantMap& strategy);
void restoreStrategyExtrasFromParameters(QVariantMap& strategy);
QString persistedStrategyIdKey();
QString persistedEngineStrategyIdKey();
QString persistedStrategyCodeKey();
QString persistedMetadataJsonKey();
QString persistedStrategyIdentityJsonKey();
QString persistedRuntimeJsonKey();
QString persistedStrategyTypeIndexKey();
QString persistedStrategyBehaviorKindKey();
QString persistedParametersKey();
QString persistedVersionKey();
QString persistedAuthorKey();
QString persistedLanguageKey();
QString persistedStatusRawKey();
QString persistedStatusIndexKey();
QString persistedCreatedAtKey();
QString persistedUpdatedAtKey();
void applyRuntimeIndexesToMap(QVariantMap& target, const StrategyRuntimeProperties& indexes);
bool tryReadPersistedFactorIds(const QVariant& rawValue,
                               std::vector<domain::strategies::FactorId>& out);
bool tryReadPersistedRuleIds(const QVariant& rawValue,
                             std::vector<domain::strategies::RuleId>& out);

strategy_view::StrategyLifecycleStatus persistedStatusFromDatabase(const QVariant& rawStatus)
{
    return lifecycleStatusFromPersistedCode(persistedStatusCodeFromDatabase(rawStatus));
}

void assignStrategyStatusIndex(QVariantMap& strategy, strategy_view::StrategyLifecycleStatus status)
{
    strategy.remove(persistedStatusRawKey());
    if (strategy_view::isKnownStrategyLifecycleStatus(status)) {
        strategy.insert(persistedStatusIndexKey(), strategy_view::strategyLifecycleStatusIndex(status));
    } else {
        strategy.remove(persistedStatusIndexKey());
    }
}

void assignStrategyBaseFields(QVariantMap& strategy, const QSqlQuery& query)
{
    strategy.insert(persistedStrategyIdKey(), query.value(persistedStrategyIdKey()));
    strategy.insert(persistedEngineStrategyIdKey(), query.value(persistedEngineStrategyIdKey()));
    strategy.insert(persistedStrategyCodeKey(), query.value(persistedStrategyCodeKey()));
    strategy.insert(persistedVersionKey(), query.value(persistedVersionKey()));
    strategy.insert(persistedAuthorKey(), query.value(persistedAuthorKey()));
    strategy.insert(persistedLanguageKey(), query.value(persistedLanguageKey()));
    assignStrategyStatusIndex(strategy, persistedStatusFromDatabase(query.value(persistedStatusRawKey())));
    strategy.insert(persistedCreatedAtKey(), query.value(persistedCreatedAtKey()));
    strategy.insert(persistedUpdatedAtKey(), query.value(persistedUpdatedAtKey()));

    assignParsedJsonObjectField(strategy,
                                persistedMetadataJsonKey(),
                                query.value(persistedMetadataJsonKey()).toString());
    assignParsedJsonObjectField(strategy,
                                persistedStrategyIdentityJsonKey(),
                                query.value(persistedStrategyIdentityJsonKey()).toString());
    assignParsedJsonObjectField(strategy,
                                persistedRuntimeJsonKey(),
                                query.value(persistedRuntimeJsonKey()).toString());
}

void assignPersistedRuntimeIndex(QVariantMap& target,
                                 const QString& key,
                                 const QVariant& rawValue)
{
    const int index = rawValue.toInt();
    if (index > 0) {
        target.insert(key, index);
    } else {
        target.remove(key);
    }
}

QString persistedAssetTypeIndexKey()
{
    return QStringLiteral("assetTypeIndex");
}

QString persistedTimeFrameIndexKey()
{
    return QStringLiteral("timeFrameIndex");
}

QString persistedRiskLevelIndexKey()
{
    return QStringLiteral("riskLevelIndex");
}

QString persistedFactorIdsKey()
{
    return QStringLiteral("factorIds");
}

QString persistedRuleIdsKey()
{
    return QStringLiteral("ruleIds");
}

QString rejectedLegacyAssetTypeKey()
{
    return QStringLiteral("asset_type");
}

QString rejectedLegacyTimeFrameKey()
{
    return QStringLiteral("time_frame");
}

QString rejectedLegacyRiskLevelKey()
{
    return QStringLiteral("risk_level");
}

QString runtimeAssetTypeKey()
{
    return QStringLiteral("assetType");
}

QString runtimeTimeFrameKey()
{
    return QStringLiteral("timeFrame");
}

QString runtimeRiskLevelKey()
{
    return QStringLiteral("riskLevel");
}

StrategyRuntimeProperties runtimeIndexesFromMap(const QVariantMap& map)
{
    StrategyRuntimeProperties indexes;
    indexes.assetTypeIndex = map.value(persistedAssetTypeIndexKey()).toInt();
    indexes.timeFrameIndex = map.value(persistedTimeFrameIndexKey()).toInt();
    indexes.riskLevelIndex = map.value(persistedRiskLevelIndexKey()).toInt();
    return indexes;
}

bool requireRuntimeIndexForRejectedRawText(const QVariantMap& strategyMap,
                                   const QString& rejectedLegacyKey,
                                   const QString& runtimeKey,
                                   const QString& indexKey,
                                   const char* failureMessage)
{
    if ((strategyMap.contains(rejectedLegacyKey) || strategyMap.contains(runtimeKey))
        && !strategyMap.contains(indexKey)) {
        qWarning() << failureMessage;
        return false;
    }
    return true;
}

bool validateRuntimeIndexRange(int indexValue,
                               int maximumIndex,
                               const char* failureMessage)
{
    if (indexValue <= 0) {
        return true;
    }

    if (indexValue > maximumIndex) {
        qWarning() << failureMessage;
        return false;
    }

    return true;
}

StrategyRuntimeProperties runtimeIndexesFromStrategy(const QVariantMap& strategy)
{
    return runtimeIndexesFromMap(strategy);
}

StrategyRuntimeProperties runtimeIndexesFromParameters(const QVariantMap& parameters)
{
    return runtimeIndexesFromMap(parameters);
}

void applyRuntimeIndexesToMap(QVariantMap& target, const StrategyRuntimeProperties& indexes)
{
    assignPersistedRuntimeIndex(target,
                                persistedAssetTypeIndexKey(),
                                indexes.assetTypeIndex);
    assignPersistedRuntimeIndex(target,
                                persistedTimeFrameIndexKey(),
                                indexes.timeFrameIndex);
    assignPersistedRuntimeIndex(target,
                                persistedRiskLevelIndexKey(),
                                indexes.riskLevelIndex);
}

PersistedStrategyData hydrateStrategyDataFromMap(const QVariantMap& strategyMap)
{
    QVariantMap normalized = strategyMap;
    if (normalized.contains(QStringLiteral("parameters"))) {
        restoreStrategyExtrasFromParameters(normalized);
    }
    return PersistedStrategyData::fromVariantMap(normalized);
}

PersistedStrategyData hydrateStrategyDataWithParameters(PersistedStrategyData strategy,
                                              const QVariantMap& parameters)
{
    if (parameters.isEmpty()) {
        return strategy;
    }

    QVariantMap strategyMap = strategy.toVariantMap();
    strategyMap.insert(persistedParametersKey(), parameters);
    return hydrateStrategyDataFromMap(strategyMap);
}

QString storedTypeText(StrategyStoredType storedType)
{
    switch (storedType) {
    case StrategyStoredType::DOUBLE_MOVING_AVERAGE:
        return QStringLiteral("DOUBLE_MOVING_AVERAGE");
    case StrategyStoredType::TURTLE_BREAKOUT:
        return QStringLiteral("TURTLE_BREAKOUT");
    case StrategyStoredType::BOLLINGER_BAND_MEAN_REVERSION:
        return QStringLiteral("BOLLINGER_BAND_MEAN_REVERSION");
    case StrategyStoredType::RSI_MEAN_REVERSION:
        return QStringLiteral("RSI_MEAN_REVERSION");
    case StrategyStoredType::MULTI_FACTOR_SELECTION:
        return QStringLiteral("MULTI_FACTOR_SELECTION");
    case StrategyStoredType::EARNINGS_SURPRISE:
        return QStringLiteral("EARNINGS_SURPRISE");
    case StrategyStoredType::STATISTICAL_PAIR_TRADING:
        return QStringLiteral("STATISTICAL_PAIR_TRADING");
    case StrategyStoredType::RISK_PARITY_ALLOCATION:
        return QStringLiteral("RISK_PARITY_ALLOCATION");
    case StrategyStoredType::MACHINE_LEARNING_SELECTION:
        return QStringLiteral("MACHINE_LEARNING_SELECTION");
    case StrategyStoredType::ORDER_FLOW_IMBALANCE:
        return QStringLiteral("ORDER_FLOW_IMBALANCE");
    case StrategyStoredType::VOLATILITY_SPREAD:
        return QStringLiteral("VOLATILITY_SPREAD");
    case StrategyStoredType::Portfolio:
        return QStringLiteral("PORTFOLIO");
    case StrategyStoredType::Custom:
        return QStringLiteral("CUSTOM");
    case StrategyStoredType::Unknown:
    default:
        return {};
    }
}

QString persistedStrategyTypeIndexKey()
{
    return QString::fromUtf8(domain::backtest::detail::kStrategyTypeIndexKey);
}

QString persistedStrategyIdKey()
{
    return QStringLiteral("strategyId");
}

QString persistedEngineStrategyIdKey()
{
    return QStringLiteral("engineStrategyId");
}

QString persistedStrategyCodeKey()
{
    return QStringLiteral("strategyCode");
}

QString persistedMetadataJsonKey()
{
    return QStringLiteral("metadata");
}

QString persistedStrategyIdentityJsonKey()
{
    return QStringLiteral("strategyIdentity");
}

QString persistedRuntimeJsonKey()
{
    return QStringLiteral("runtime");
}

QString persistedVersionKey()
{
    return QStringLiteral("version");
}

QString persistedAuthorKey()
{
    return QStringLiteral("author");
}

QString persistedLanguageKey()
{
    return QStringLiteral("language");
}

QString persistedStatusRawKey()
{
    return QStringLiteral("status");
}

QString persistedStatusIndexKey()
{
    return QStringLiteral("statusIndex");
}

QString persistedCreatedAtKey()
{
    return QStringLiteral("createdAt");
}

QString persistedUpdatedAtKey()
{
    return QStringLiteral("updatedAt");
}

QString persistedParametersKey()
{
    return QStringLiteral("parameters");
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

void assignStrategyParameters(QVariantMap& strategy, const QVariantMap& parameters)
{
    strategy.insert(persistedParametersKey(), parameters);
    if (!parameters.isEmpty()) {
        restoreStrategyExtrasFromParameters(strategy);
    }
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

QString selectStrategiesWithoutParametersSql()
{
    return QStringLiteral("SELECT %1 FROM strategy ORDER BY created_at DESC")
        .arg(persistedStrategyBaseColumnsSql());
}

QString selectStrategiesWithoutParametersSql(const QString& suffix)
{
    return QStringLiteral("SELECT %1 FROM strategy %2")
        .arg(persistedStrategyBaseColumnsSql(), suffix);
}

QString selectStrategiesWithParametersTextSql()
{
    return QStringLiteral(
               "SELECT %1, CAST(parameters AS CHAR) as parameters_text "
               "FROM strategy ORDER BY created_at DESC")
        .arg(persistedStrategyBaseColumnsSql());
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

const QStringList& persistedParameterPassthroughKeys()
{
    static const QStringList keys = {
        QStringLiteral("optimization_method"),
        QStringLiteral("backtest_settings"),
        persistedPerformanceMetricsKey(),
        QStringLiteral("tags")
    };
    return keys;
}

QVariantList toPersistedFactorIdVariantList(const std::vector<domain::strategies::FactorId>& values)
{
    QVariantList list;
    list.reserve(static_cast<qsizetype>(values.size()));
    for (const domain::strategies::FactorId value : values) {
        list.push_back(QVariant::fromValue<qulonglong>(value));
    }
    return list;
}

QVariantList toPersistedRuleIdVariantList(const std::vector<domain::strategies::RuleId>& values)
{
    QVariantList list;
    list.reserve(static_cast<qsizetype>(values.size()));
    for (const domain::strategies::RuleId value : values) {
        list.push_back(QVariant::fromValue<qulonglong>(value));
    }
    return list;
}

QString toCompactJsonText(const QVariantMap& value)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(value)).toJson(QJsonDocument::Compact));
}

QVariantMap metadataToVariantMap(const domain::strategies::StrategyMetadata& metadata)
{
    QVariantMap map;
    map.insert(QStringLiteral("name"), QString::fromStdString(metadata.name));
    map.insert(QStringLiteral("description"), QString::fromStdString(metadata.description));
    map.insert(QStringLiteral("behaviorKind"), static_cast<int>(metadata.behaviorKind));
    map.insert(QStringLiteral("factorIds"), toPersistedFactorIdVariantList(metadata.factorIds));
    map.insert(QStringLiteral("ruleIds"), toPersistedRuleIdVariantList(metadata.ruleIds));
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

    tryReadPersistedFactorIds(map.value(persistedFactorIdsKey()), metadata.factorIds);
    tryReadPersistedRuleIds(map.value(persistedRuleIdsKey()), metadata.ruleIds);
    return metadata;
}

QVariantMap strategyIdentityToVariantMap(const ResolvedStrategyIdentity& strategyIdentity)
{
    QVariantMap map;
    if (!strategyIdentity.validStoredType) {
        return map;
    }

    map.insert(persistedStrategyTypeIndexKey(), strategyIdentity.storedTypeIndex());
    if (strategyIdentity.behavior.valid) {
        map.insert(persistedStrategyBehaviorKindKey(), strategyIdentity.behavior.index());
    }
    return map;
}

QVariantMap runtimeToVariantMap(const StrategyRuntimeProperties& runtime)
{
    QVariantMap map;
    applyRuntimeIndexesToMap(map, runtime);
    return map;
}

bool tryReadPersistedFactorIds(const QVariant& rawValue,
                               std::vector<domain::strategies::FactorId>& out)
{
    if (!rawValue.isValid() || rawValue.isNull()) {
        out.clear();
        return true;
    }

    const QVariantList rawList = rawValue.toList();
    std::vector<domain::strategies::FactorId> parsed;
    parsed.reserve(static_cast<size_t>(rawList.size()));
    for (const QVariant& item : rawList) {
        bool ok = false;
        const qulonglong value = item.toULongLong(&ok);
        if (!ok || value == 0ULL) {
            return false;
        }
        parsed.push_back(static_cast<domain::strategies::FactorId>(value));
    }

    out = std::move(parsed);
    return true;
}

bool tryReadPersistedRuleIds(const QVariant& rawValue,
                             std::vector<domain::strategies::RuleId>& out)
{
    if (!rawValue.isValid() || rawValue.isNull()) {
        out.clear();
        return true;
    }

    const QVariantList rawList = rawValue.toList();
    std::vector<domain::strategies::RuleId> parsed;
    parsed.reserve(static_cast<size_t>(rawList.size()));
    for (const QVariant& item : rawList) {
        bool ok = false;
        const qulonglong value = item.toULongLong(&ok);
        if (!ok || value == 0ULL) {
            return false;
        }
        parsed.push_back(static_cast<domain::strategies::RuleId>(value));
    }

    out = std::move(parsed);
    return true;
}

    QString factorOverlayParameterKey()
    {
        return QStringLiteral("factor_overlay");
    }

void clearRuntimePresentationFields(QVariantMap& strategy)
{
    strategy.remove(rejectedLegacyAssetTypeKey());
    strategy.remove(rejectedLegacyTimeFrameKey());
    strategy.remove(rejectedLegacyRiskLevelKey());
    strategy.remove(runtimeAssetTypeKey());
    strategy.remove(runtimeTimeFrameKey());
    strategy.remove(runtimeRiskLevelKey());
    strategy.remove(persistedAssetTypeIndexKey());
    strategy.remove(persistedTimeFrameIndexKey());
    strategy.remove(persistedRiskLevelIndexKey());
}

QString persistedStrategyBehaviorKindKey()
{
    return QString::fromUtf8(domain::backtest::detail::kStrategyBehaviorKindKey);
}

void clearPersistedStrategyIdentityFields(QVariantMap& strategy)
{
    strategy.remove(persistedStrategyTypeIndexKey());
    strategy.remove(persistedStrategyBehaviorKindKey());
}

void writePersistedStrategyIdentityFields(QVariantMap& strategy,
                                         const ResolvedStrategyIdentity& strategyIdentity)
{
    strategy.insert(persistedStrategyTypeIndexKey(), strategyIdentity.storedTypeIndex());
    if (strategyIdentity.behavior.valid) {
        strategy.insert(persistedStrategyBehaviorKindKey(), strategyIdentity.behavior.index());
        return;
    }

    strategy.remove(persistedStrategyBehaviorKindKey());
}

void assignPersistedStrategyIdentity(QVariantMap& strategy)
{
    const ResolvedStrategyIdentity strategyIdentity = domain::backtest::resolveStrategyIdentity(strategy);
    if (!strategyIdentity.validStoredType) {
        clearPersistedStrategyIdentityFields(strategy);
        return;
    }

    writePersistedStrategyIdentityFields(strategy, strategyIdentity);
}

QVariantMap buildPersistedParameters(const QVariantMap& strategy)
{
    QVariantMap parameters = strategy.value(persistedParametersKey()).toMap();
    const StrategyRuntimeProperties runtimeIndexes = runtimeIndexesFromStrategy(strategy);

    for (const QString& key : persistedParameterPassthroughKeys()) {
        if (!strategy.contains(key)) {
            continue;
        }

        const QVariant value = strategy.value(key);
        if (!value.isValid() || value.isNull()) {
            continue;
        }

        parameters.insert(key, value);
    }

    clearRuntimePresentationFields(parameters);

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

void restoreStrategyExtrasFromParameters(QVariantMap& strategy)
{
    QVariantMap parameters = strategy.value(persistedParametersKey()).toMap();
    const StrategyRuntimeProperties runtimeIndexes = runtimeIndexesFromParameters(parameters);
    clearRuntimePresentationFields(strategy);

    if (parameters.isEmpty()) {
        return;
    }

    applyRuntimeIndexesToMap(strategy, runtimeIndexes);

    for (const QString& key : persistedParameterPassthroughKeys()) {
        if (parameters.contains(key)) {
            strategy.insert(key, parameters.value(key));
        }
    }

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
        strategyMap.insert(persistedStrategyIdKey(), QString::fromStdString(strategyId));
    }
    if (engineStrategyId > 0ULL) {
        strategyMap.insert(persistedEngineStrategyIdKey(), QVariant::fromValue<qulonglong>(engineStrategyId));
    }
    if (!strategyCode.empty()) {
        strategyMap.insert(persistedStrategyCodeKey(), QString::fromStdString(strategyCode));
    }
    const QVariantMap metadataMap = metadataToVariantMap(metadata);
    strategyMap.insert(persistedMetadataJsonKey(), metadataMap);
    const QString strategyName = QString::fromStdString(metadata.name);
    if (!strategyName.trimmed().isEmpty()) {
        strategyMap.insert(QStringLiteral("strategyName"), strategyName);
    }
    const QString description = QString::fromStdString(metadata.description);
    if (!description.trimmed().isEmpty()) {
        strategyMap.insert(QStringLiteral("description"), description);
    }
    if (!version.empty()) {
        strategyMap.insert(persistedVersionKey(), QString::fromStdString(version));
    }
    if (!author.empty()) {
        strategyMap.insert(persistedAuthorKey(), QString::fromStdString(author));
    }
    switch (language) {
    case StrategyLanguageCode::Cpp:
        strategyMap.insert(persistedLanguageKey(), persistedLanguageText(StrategyLanguageCode::Cpp));
        break;
    case StrategyLanguageCode::Julia:
        strategyMap.insert(persistedLanguageKey(), persistedLanguageText(StrategyLanguageCode::Julia));
        break;
    case StrategyLanguageCode::R:
        strategyMap.insert(persistedLanguageKey(), persistedLanguageText(StrategyLanguageCode::R));
        break;
    case StrategyLanguageCode::Python:
    default:
        strategyMap.insert(persistedLanguageKey(), persistedLanguageText(StrategyLanguageCode::Python));
        break;
    }

    if (strategy_view::isKnownStrategyLifecycleStatus(status)) {
        strategyMap.insert(persistedStatusIndexKey(), strategy_view::strategyLifecycleStatusIndex(status));
    }

    if (createdAt.isValid()) {
        strategyMap.insert(persistedCreatedAtKey(), createdAt);
    }
    if (updatedAt.isValid()) {
        strategyMap.insert(persistedUpdatedAtKey(), updatedAt);
    }

    if (strategyIdentity.validStoredType) {
        const QVariantMap identityMap = strategyIdentityToVariantMap(strategyIdentity);
        strategyMap.insert(persistedStrategyIdentityJsonKey(), identityMap);
        writePersistedStrategyIdentityFields(strategyMap, strategyIdentity);
    }

    applyRuntimeIndexesToMap(persistedParameters, runtime);
    strategyMap.insert(persistedRuntimeJsonKey(), runtimeToVariantMap(runtime));
    strategyMap.insert(persistedParametersKey(), persistedParameters);
    if (!performanceMetrics.isEmpty()) {
        strategyMap.insert(persistedPerformanceMetricsKey(), performanceMetrics);
    }

    applyRuntimeIndexesToMap(strategyMap, runtime);
    return strategyMap;
}

PersistedStrategyData PersistedStrategyData::fromVariantMap(const QVariantMap& strategyMap)
{
    PersistedStrategyData data;
    data.strategyId = strategyMap.value(persistedStrategyIdKey()).toString().toStdString();
    data.engineStrategyId = strategyMap.value(persistedEngineStrategyIdKey()).toULongLong();
    data.strategyCode = strategyMap.value(persistedStrategyCodeKey()).toString().toStdString();

    data.metadata = metadataFromVariantMap(strategyMap.value(persistedMetadataJsonKey()).toMap());

    data.strategyIdentity = domain::backtest::resolveStrategyIdentity(
        strategyMap.value(persistedStrategyIdentityJsonKey()).toMap());

    data.version = strategyMap.value(persistedVersionKey()).toString().toStdString();
    data.author = strategyMap.value(persistedAuthorKey()).toString().toStdString();
    data.language = persistedLanguageCodeFromRaw(strategyMap.value(persistedLanguageKey()).toString());
    data.status = strategy_view::resolveStrategyLifecycleStatus(strategyMap.value(persistedStatusIndexKey()));
    data.createdAt = strategyMap.value(persistedCreatedAtKey()).toDateTime();
    data.updatedAt = strategyMap.value(persistedUpdatedAtKey()).toDateTime();
    data.parameters = strategyMap.value(persistedParametersKey()).toMap();
    data.performanceMetrics = strategyMap.value(persistedPerformanceMetricsKey()).toMap();
    data.runtime = runtimeIndexesFromStrategy(strategyMap.value(persistedRuntimeJsonKey()).toMap());
    if (!data.runtime.hasAny()) {
        data.runtime = runtimeIndexesFromParameters(data.parameters);
    }

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
        return hydrateStrategyDataWithParameters(rowToStrategyData(query),
                                                 loadStrategyParameters(strategyId, conn.get()));
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
        QString strategyId = query.value(persistedStrategyIdKey()).toString();
        return hydrateStrategyDataWithParameters(rowToStrategyData(query),
                                                 loadStrategyParameters(strategyId, conn.get()));
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
    qWarning() << "[StrategyRepository] findAll db host=" << db.hostName()
               << "port=" << db.port()
               << "name=" << db.databaseName()
               << "user=" << db.userName();

    {
        QSqlQuery countQuery(db);
        if (countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM strategy")) && countQuery.next()) {
            qWarning() << "[StrategyRepository] findAll precheck strategy count=" << countQuery.value(0).toInt();
        } else {
            qWarning() << "[StrategyRepository] findAll precheck count failed:" << countQuery.lastError().text();
        }
    }

    QSqlQuery query(db);
    const QString sql = selectStrategiesSql();
    qWarning() << "[StrategyRepository] findAll sql=" << sql;
    query.prepare(sql);
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to find all strategies:" << query.lastError().text();
        return {};
    }

    std::vector<PersistedStrategyData> strategies;
    while (query.next()) {
        strategies.push_back(rowToStrategyData(query));
    }

    qWarning() << "[StrategyRepository] findAll fetched rows=" << static_cast<int>(strategies.size());

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
            .arg(persistedStrategyTypeIndexKey())));
    query.addBindValue(static_cast<int>(strategyType));
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to find strategies by type:" << query.lastError().text();
        return {};
    }
    
    std::vector<PersistedStrategyData> strategies;
    while (query.next()) {
        QString strategyId = query.value(persistedStrategyIdKey()).toString();
        strategies.push_back(hydrateStrategyDataWithParameters(rowToStrategyData(query),
                                                               loadStrategyParameters(strategyId, conn.get())));
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
    bindPersistedStatusCode(query, statusCode);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to find strategies by status:" << query.lastError().text();
        return {};
    }
    
    std::vector<PersistedStrategyData> strategies;
    while (query.next()) {
        QString strategyId = query.value(persistedStrategyIdKey()).toString();
        strategies.push_back(hydrateStrategyDataWithParameters(rowToStrategyData(query),
                                                               loadStrategyParameters(strategyId, conn.get())));
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
        QString strategyId = query.value(persistedStrategyIdKey()).toString();
        strategies.push_back(hydrateStrategyDataWithParameters(rowToStrategyData(query),
                                                               loadStrategyParameters(strategyId, conn.get())));
    }
    
    return strategies;
}

QString StrategyRepository::save(const PersistedStrategyData& strategy) {
    if (!validateStrategy(strategy)) {
        qWarning() << "[StrategyRepository] Invalid strategy data";
        return QString();
    }
    
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
        updatedStrategy[persistedParametersKey()] = existingParameters;
        restoreStrategyExtrasFromParameters(updatedStrategy);
    }

    const QVariantMap incomingStrategy = strategy.toVariantMap();

    for (auto it = incomingStrategy.begin(); it != incomingStrategy.end(); ++it) {
        if (it.key() == persistedParametersKey()) {
            updatedStrategy[it.key()] = mergeVariantMapsRecursive(existingParameters, it.value().toMap());
            continue;
        }
        updatedStrategy[it.key()] = it.value();
    }
    updatedStrategy[persistedStrategyIdKey()] = strategyId;
    
    QString resultId = saveStrategyInternal(PersistedStrategyData::fromVariantMap(updatedStrategy), db, true);
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
    if (!deleteStrategyParameters(strategyId, db)) {
        qWarning() << "[StrategyRepository] Failed to delete strategy parameters";
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
    
    // 清空参数
    QSqlQuery clearParamsQuery(db);
    if (!clearParamsQuery.exec("DELETE FROM backtest_config WHERE config_id IN (SELECT config_id FROM backtest_config)")) {
        qWarning() << "[StrategyRepository] Failed to clear backtest config:" << clearParamsQuery.lastError().text();
        db.rollback();
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
    bindPersistedStatusCode(query, statusCode);
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
    int parametersColumn = query.record().indexOf("parameters");
    if (parametersColumn >= 0) {
        QString parametersJson = query.value(parametersColumn).toString();
        if (!parametersJson.isEmpty()) {
            QVariantMap parameters;
            if (tryParseJsonObjectVariantMap(parametersJson, parameters)) {
                assignStrategyParameters(strategy, parameters);
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
    
    QString parametersJson = query.value("parameters_text").toString();
    if (parametersJson.isEmpty()) {
        return QVariantMap();
    }

    QVariantMap parameters;
    if (!tryParseJsonObjectVariantMap(parametersJson, parameters)) {
        return QVariantMap();
    }

    return parameters;
}

bool StrategyRepository::saveStrategyParameters(const QString& strategyId, const QVariantMap& parameters, QSqlDatabase& db) {
    // 将参数转换为 JSON 字符串
    QJsonObject jsonObj = QJsonObject::fromVariantMap(parameters);
    QJsonDocument doc(jsonObj);
    QString parametersJson = doc.toJson(QJsonDocument::Compact);
    
    QSqlQuery query(db);
    query.prepare("UPDATE strategy SET parameters = ? WHERE strategy_id = ?");
    query.addBindValue(parametersJson);
    query.addBindValue(strategyId);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to save strategy parameters:" << query.lastError().text();
        return false;
    }
    
    return true;
}

bool StrategyRepository::deleteStrategyParameters(const QString& strategyId, QSqlDatabase& db) {
    // 参数保存在 strategy 表的 JSON 字段中，更新为空 JSON 即可
    QSqlQuery query(db);
    query.prepare("UPDATE strategy SET parameters = '{}' WHERE strategy_id = ?");
    query.addBindValue(strategyId);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to delete strategy parameters:" << query.lastError().text();
        return false;
    }
    
    return true;
}

QString StrategyRepository::saveStrategyInternal(const PersistedStrategyData& strategy, QSqlDatabase& db, bool isUpdate) {
    const QVariantMap strategyMap = strategy.toVariantMap();
    QString strategyId = QString::fromStdString(strategy.strategyId);
    QString strategyCode = QString::fromStdString(strategy.strategyCode);
    QString strategyName = QString::fromStdString(strategy.metadata.name);
    const QString metadataJson = toCompactJsonText(strategyMap.value(persistedMetadataJsonKey()).toMap());
    const QString strategyIdentityJson = toCompactJsonText(strategyMap.value(persistedStrategyIdentityJsonKey()).toMap());
    const QString runtimeJson = toCompactJsonText(strategyMap.value(persistedRuntimeJsonKey()).toMap());
    const QString performanceJson = toCompactJsonText(strategy.performanceMetrics);
    QString version = QString::fromStdString(strategy.version);
    QString author = QString::fromStdString(strategy.author);
    const StrategyLanguageCode languageCode = strategy.language;
    const ResolvedStrategyIdentity strategyIdentity = strategy.strategyIdentity;
    const strategy_view::StrategyLifecycleStatus strategyStatus = strategy.status;
    const PersistedStrategyStatusCode statusCode = persistedStatusCodeFromLifecycle(strategyStatus);
    

    if (strategyName.isEmpty() || !strategyIdentity.validStoredType) {
        qWarning() << "[StrategyRepository] Strategy name and type are required";
        return QString();
    }
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
        bindPersistedLanguageCode(query, languageCode);
        bindPersistedStatusCode(query, statusCode);
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
        bindPersistedLanguageCode(query, languageCode);
        bindPersistedStatusCode(query, statusCode);
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
        if (!deleteStrategyParameters(strategyId, db)) {
            return QString();
        }
    }

    if (!parameters.isEmpty()) {
        if (!saveStrategyParameters(strategyId, parameters, db)) {
            return QString();
        }
    }
    
    return strategyId;
}

bool StrategyRepository::validateStrategy(const PersistedStrategyData& strategy) const {
    const QVariantMap strategyMap = strategy.toVariantMap();
    if (strategy.metadata.name.empty()) {
        qWarning() << "[StrategyRepository] Validation failed: metadata.name is required";
        return false;
    }
    
    const ResolvedStrategyIdentity strategyIdentity = strategy.strategyIdentity;
    if (!strategyIdentity.validStoredType) {
        qWarning() << "[StrategyRepository] Validation failed: strategyTypeIndex or strategyBehaviorKind is required";
        return false;
    }
    
    if (strategyMap.contains(persistedStatusRawKey()) && !strategyMap.contains(persistedStatusIndexKey())) {
        qWarning() << "[StrategyRepository] Validation failed: statusIndex is required; raw status text is not accepted";
        return false;
    }

    if (strategyMap.contains(persistedStatusIndexKey())) {
        const strategy_view::StrategyLifecycleStatus status = strategy.status;
        if (persistedStatusCodeFromLifecycle(status) == PersistedStrategyStatusCode::Unknown) {
            qWarning() << "[StrategyRepository] Validation failed: invalid persisted statusIndex";
            return false;
        }
    }

    if (!requireRuntimeIndexForRejectedRawText(strategyMap,
                                       rejectedLegacyAssetTypeKey(),
                                       runtimeAssetTypeKey(),
                                       persistedAssetTypeIndexKey(),
                                       "[StrategyRepository] Validation failed: assetTypeIndex is required; raw asset type text is not accepted")) {
        return false;
    }

    if (!validateRuntimeIndexRange(strategy.runtime.assetTypeIndex,
                                   6,
                                   "[StrategyRepository] Validation failed: invalid assetTypeIndex")) {
        return false;
    }

    if (!requireRuntimeIndexForRejectedRawText(strategyMap,
                                       rejectedLegacyTimeFrameKey(),
                                       runtimeTimeFrameKey(),
                                       persistedTimeFrameIndexKey(),
                                       "[StrategyRepository] Validation failed: timeFrameIndex is required; raw time frame text is not accepted")) {
        return false;
    }

    if (!validateRuntimeIndexRange(strategy.runtime.timeFrameIndex,
                                   10,
                                   "[StrategyRepository] Validation failed: invalid timeFrameIndex")) {
        return false;
    }

    if (!requireRuntimeIndexForRejectedRawText(strategyMap,
                                       rejectedLegacyRiskLevelKey(),
                                       runtimeRiskLevelKey(),
                                       persistedRiskLevelIndexKey(),
                                       "[StrategyRepository] Validation failed: riskLevelIndex is required; raw risk level text is not accepted")) {
        return false;
    }

    if (!validateRuntimeIndexRange(strategy.runtime.riskLevelIndex,
                                   4,
                                   "[StrategyRepository] Validation failed: invalid riskLevelIndex")) {
        return false;
    }
    
    return true;
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

