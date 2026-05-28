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
    if (normalized == persistedLanguageText(StrategyLanguageCode::Cpp)
        || normalized == QStringLiteral("C++")) {
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

bool bindPersistedStoredType(QSqlQuery& query, StrategyStoredType storedType)
{
    const QString persistedText = storedTypeText(storedType);
    if (persistedText.isEmpty()) {
        return false;
    }

    query.addBindValue(persistedText);
    return true;
}

void assignPersistedStrategyIdentity(QVariantMap& strategy);
void restoreStrategyExtrasFromParameters(QVariantMap& strategy);
QString persistedStrategyIdKey();
QString persistedEngineStrategyIdKey();
QString persistedStrategyCodeKey();
QString persistedStrategyNameKey();
QString persistedStrategyTypeTextKey();
QString persistedParametersKey();
QString persistedDescriptionKey();
QString persistedVersionKey();
QString persistedAuthorKey();
QString persistedLanguageKey();
QString persistedStatusRawKey();
QString persistedStatusIndexKey();
QString persistedCreatedAtKey();
QString persistedUpdatedAtKey();

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
    strategy.insert(persistedStrategyNameKey(), query.value(persistedStrategyNameKey()));
    strategy.insert(persistedStrategyTypeTextKey(), query.value(persistedStrategyTypeTextKey()));
    strategy.insert(persistedDescriptionKey(), query.value(persistedDescriptionKey()));
    strategy.insert(persistedVersionKey(), query.value(persistedVersionKey()));
    strategy.insert(persistedAuthorKey(), query.value(persistedAuthorKey()));
    strategy.insert(persistedLanguageKey(), query.value(persistedLanguageKey()));
    assignStrategyStatusIndex(strategy, persistedStatusFromDatabase(query.value(persistedStatusRawKey())));
    strategy.insert(persistedCreatedAtKey(), query.value(persistedCreatedAtKey()));
    strategy.insert(persistedUpdatedAtKey(), query.value(persistedUpdatedAtKey()));
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

QString legacyAssetTypeKey()
{
    return QStringLiteral("asset_type");
}

QString legacyTimeFrameKey()
{
    return QStringLiteral("time_frame");
}

QString legacyRiskLevelKey()
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

bool requireRuntimeIndexForRawText(const QVariantMap& strategyMap,
                                   const QString& legacyKey,
                                   const QString& runtimeKey,
                                   const QString& indexKey,
                                   const char* failureMessage)
{
    if ((strategyMap.contains(legacyKey) || strategyMap.contains(runtimeKey))
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

void assignPersistedStrategyIdentityToData(StrategyData& strategyData)
{
    if (!strategyData.strategyIdentity.validStoredType) {
        return;
    }

    if (!strategyData.strategyIdentity.behavior.valid) {
        strategyData.strategyIdentity = domain::backtest::resolveStrategyIdentity(strategyData.toVariantMap());
    }
}

StrategyData hydrateStrategyDataFromMap(const QVariantMap& strategyMap)
{
    QVariantMap normalized = strategyMap;
    if (normalized.contains(QStringLiteral("parameters"))) {
        restoreStrategyExtrasFromParameters(normalized);
    }
    assignPersistedStrategyIdentity(normalized);
    return StrategyData::fromVariantMap(normalized);
}

StrategyData hydrateStrategyDataWithParameters(StrategyData strategy,
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
    case StrategyStoredType::TrendFollowing:
        return QStringLiteral("TREND");
    case StrategyStoredType::MeanReversion:
        return QStringLiteral("MEAN_REVERSION");
    case StrategyStoredType::Alpha:
        return QStringLiteral("ALPHA");
    case StrategyStoredType::Arbitrage:
        return QStringLiteral("ARBITRAGE");
    case StrategyStoredType::HighFrequency:
        return QStringLiteral("HFT");
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
    return QStringLiteral("strategy_id");
}

QString persistedEngineStrategyIdKey()
{
    return QStringLiteral("engine_strategy_id");
}

QString persistedStrategyCodeKey()
{
    return QStringLiteral("strategy_code");
}

QString persistedStrategyNameKey()
{
    return QStringLiteral("strategy_name");
}

QString persistedStrategyTypeTextKey()
{
    return QStringLiteral("strategy_type");
}

QString persistedDescriptionKey()
{
    return QStringLiteral("description");
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
    return QStringLiteral("created_at");
}

QString persistedUpdatedAtKey()
{
    return QStringLiteral("updated_at");
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
    return QStringLiteral("performance_metrics");
}

QString persistedStrategyBaseColumnsSql()
{
    return QStringLiteral(
    "strategy_id, engine_strategy_id, strategy_code, strategy_name, strategy_type, "
        "description, version, author, language, status, "
        "created_at, updated_at");
}

QString selectStrategiesSql()
{
    return QStringLiteral("SELECT %1, parameters FROM strategy ORDER BY created_at DESC")
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
        "strategy_name = ?, "
        "strategy_type = ?, "
        "description = ?, "
        "version = ?, "
        "author = ?, "
        "language = ?, "
        "status = ?");
}

QString persistedStrategyInsertColumnsSql()
{
    return QStringLiteral(
        "strategy_id, strategy_code, strategy_name, strategy_type, "
        "description, version, author, language, status, "
        "created_at, updated_at");
}

QString persistedStrategyInsertValuesSql()
{
    return QStringLiteral("?, ?, ?, ?, ?, ?, ?, ?, ?, NOW(), NOW()");
}

const QStringList& persistedParameterPassthroughKeys()
{
    static const QStringList keys = {
        QStringLiteral("optimization_method"),
        QStringLiteral("backtest_settings"),
        QStringLiteral("advanced_options"),
        persistedPerformanceMetricsKey(),
        QStringLiteral("tags")
    };
    return keys;
}

    QString factorOverlayParameterKey()
    {
        return QStringLiteral("factor_overlay");
    }

    QString factorOverlaySnapshotKey()
    {
        return QStringLiteral("factorOverlaySnapshot");
    }

void clearRuntimePresentationFields(QVariantMap& strategy)
{
    strategy.remove(legacyAssetTypeKey());
    strategy.remove(legacyTimeFrameKey());
    strategy.remove(legacyRiskLevelKey());
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

    const QVariantMap factorOverlay = parameters.value(factorOverlayParameterKey()).toMap();
    if (!factorOverlay.isEmpty()) {
        strategy.insert(factorOverlaySnapshotKey(), factorOverlay);
    }
}

}

bool StrategyData::isValid() const
{
    return !strategyName.isEmpty() && strategyIdentity.validStoredType;
}

QVariantMap StrategyData::toVariantMap() const
{
    QVariantMap strategyMap;
    QVariantMap persistedParameters = parameters;
    if (!strategyId.isEmpty()) {
        strategyMap.insert(persistedStrategyIdKey(), strategyId);
    }
    if (engineStrategyId > 0ULL) {
        strategyMap.insert(persistedEngineStrategyIdKey(), QVariant::fromValue<qulonglong>(engineStrategyId));
        strategyMap.insert(QStringLiteral("engineStrategyId"), QVariant::fromValue<qulonglong>(engineStrategyId));
    }
    if (!strategyCode.isEmpty()) {
        strategyMap.insert(persistedStrategyCodeKey(), strategyCode);
    }
    strategyMap.insert(persistedStrategyNameKey(), strategyName);
    if (!description.isEmpty()) {
        strategyMap.insert(persistedDescriptionKey(), description);
    }
    if (!version.isEmpty()) {
        strategyMap.insert(persistedVersionKey(), version);
    }
    if (!author.isEmpty()) {
        strategyMap.insert(persistedAuthorKey(), author);
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
        writePersistedStrategyIdentityFields(strategyMap, strategyIdentity);
        const QString persistedStoredType = storedTypeText(strategyIdentity.storedType);
        if (!persistedStoredType.isEmpty()) {
            strategyMap.insert(persistedStrategyTypeTextKey(), persistedStoredType);
        }
    }

    applyRuntimeIndexesToMap(persistedParameters, runtime);
    strategyMap.insert(persistedParametersKey(), persistedParameters);
    if (!performanceMetrics.isEmpty()) {
        strategyMap.insert(persistedPerformanceMetricsKey(), performanceMetrics);
    }

    applyRuntimeIndexesToMap(strategyMap, runtime);
    return strategyMap;
}

StrategyData StrategyData::fromVariantMap(const QVariantMap& strategyMap)
{
    StrategyData strategyData;
    strategyData.strategyId = strategyMap.value(persistedStrategyIdKey()).toString();
    strategyData.engineStrategyId = strategyMap.value(
        persistedEngineStrategyIdKey(),
        strategyMap.value(QStringLiteral("engineStrategyId"))).toULongLong();
    strategyData.strategyCode = strategyMap.value(persistedStrategyCodeKey()).toString();
    strategyData.strategyName = strategyMap.value(persistedStrategyNameKey()).toString();
    strategyData.strategyIdentity = domain::backtest::resolveStrategyIdentity(strategyMap);
    strategyData.description = strategyMap.value(persistedDescriptionKey()).toString();
    strategyData.version = strategyMap.value(persistedVersionKey()).toString();
    strategyData.author = strategyMap.value(persistedAuthorKey()).toString();
    strategyData.language = persistedLanguageCodeFromRaw(strategyMap.value(persistedLanguageKey()).toString());
    strategyData.status = strategy_view::resolveStrategyLifecycleStatus(strategyMap.value(persistedStatusIndexKey()));
    strategyData.createdAt = strategyMap.value(persistedCreatedAtKey()).toDateTime();
    strategyData.updatedAt = strategyMap.value(persistedUpdatedAtKey()).toDateTime();
    strategyData.parameters = strategyMap.value(persistedParametersKey()).toMap();
    strategyData.performanceMetrics = strategyMap.value(persistedPerformanceMetricsKey()).toMap();
    strategyData.runtime = runtimeIndexesFromStrategy(strategyMap);
    if (!strategyData.runtime.hasAny()) {
        strategyData.runtime = runtimeIndexesFromParameters(strategyData.parameters);
    }
    assignPersistedStrategyIdentityToData(strategyData);
    return strategyData;
}

StrategyRepository::StrategyRepository() 
    : m_initialized(false) {
}

StrategyRepository::~StrategyRepository() {
}

std::optional<StrategyData> StrategyRepository::findById(const QString& strategyId) {
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

std::optional<StrategyData> StrategyRepository::findByCode(const QString& strategyCode) {
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
        QString strategyId = query.value("strategy_id").toString();
        return hydrateStrategyDataWithParameters(rowToStrategyData(query),
                                                 loadStrategyParameters(strategyId, conn.get()));
    }
    
    return std::nullopt;
}

std::vector<StrategyData> StrategyRepository::findAll() {
    qDebug() << "[StrategyRepository::findAll] 开始查询所有策略";
    
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository::findAll] Failed to get database connection";
        return {};
    }
    
    qDebug() << "[StrategyRepository::findAll] 数据库连接成功";
    
    QSqlQuery query(conn.get());
    // 不使用SELECT *，而是明确列出所有字段，避免JSON字段问题
    const QString sql = selectStrategiesSql();
    query.prepare(sql);
    
    qDebug() << "[StrategyRepository::findAll] 准备执行SQL:" << sql;
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository::findAll] Failed to find all strategies:" << query.lastError().text();
        qWarning() << "[StrategyRepository::findAll] SQL error:" << query.lastError().databaseText();
        return {};
    }
    
    qDebug() << "[StrategyRepository::findAll] SQL执行成功";
    
    // 立即检查是否有结果
    if (query.lastError().isValid()) {
        qWarning() << "[StrategyRepository::findAll] 查询执行后有错误:" << query.lastError().text();
    }
    
    // 检查结果集是否有效
    if (!query.isActive()) {
        qWarning() << "[StrategyRepository::findAll] 查询不活动";
    }
    
    if (!query.isSelect()) {
        qWarning() << "[StrategyRepository::findAll] 查询不是SELECT类型";
    }
    
    int rowCount = 0;
    int processedCount = 0;
    std::vector<StrategyData> strategies;
    
    // 先检查查询是否有结果
    if (query.isActive() && query.isSelect()) {
        qDebug() << "[StrategyRepository::findAll] 查询是活动的SELECT查询";
    } else {
        qWarning() << "[StrategyRepository::findAll] 查询不活动或不是SELECT查询";
    }
    
    // 调试信息：显示查询结果状态
    bool hasRows = query.size() > 0;
    qDebug() << "[StrategyRepository::findAll] 查询结果集大小:" << query.size();
    
    // 获取记录信息
    QSqlRecord record = query.record();
    int fieldCount = record.count();
    qDebug() << "[StrategyRepository::findAll] 记录字段数量:" << fieldCount;
    for (int i = 0; i < fieldCount; i++) {
        QString fieldName = record.fieldName(i);
        qDebug() << "[StrategyRepository::findAll] 字段" << i << ":" << fieldName;
    }
    
    if (hasRows) {
        qDebug() << "[StrategyRepository::findAll] 查询有结果集，开始处理";
    } else {
        qDebug() << "[StrategyRepository::findAll] 查询结果集大小为0或未知，尝试遍历";
    }
    
    // 尝试逐步诊断：先尝试一个没有JSON字段的简单查询
    qDebug() << "[StrategyRepository::findAll] === 第一步：尝试没有JSON字段的查询 ===";
    QSqlQuery simpleQueryNoJson(conn.get());
    if (simpleQueryNoJson.exec("SELECT strategy_id, strategy_name FROM strategy ORDER BY created_at DESC")) {
        int simpleCount = 0;
        while (simpleQueryNoJson.next()) {
            simpleCount++;
            QString id = simpleQueryNoJson.value(0).toString();
            QString name = simpleQueryNoJson.value(1).toString();
            qDebug() << "[StrategyRepository::findAll] 简单查询(无JSON)找到策略" << simpleCount << "ID:" << id << "名称:" << name;
        }
        qDebug() << "[StrategyRepository::findAll] 简单查询(无JSON)找到" << simpleCount << "个策略";
    } else {
        qWarning() << "[StrategyRepository::findAll] 简单查询(无JSON)失败:" << simpleQueryNoJson.lastError().text();
    }
    
    // 第二步：尝试没有parameters字段的查询
    qDebug() << "[StrategyRepository::findAll] === 第二步：尝试没有parameters字段的查询 ===";
    QSqlQuery queryWithoutParams(conn.get());
    const QString sqlWithoutParams = selectStrategiesWithoutParametersSql();
    
    if (queryWithoutParams.exec(sqlWithoutParams)) {
        int countWithoutParams = 0;
        while (queryWithoutParams.next()) {
            countWithoutParams++;
            QString id = queryWithoutParams.value("strategy_id").toString();
            qDebug() << "[StrategyRepository::findAll] 无parameters查询找到策略" << countWithoutParams << "ID:" << id;
        }
        qDebug() << "[StrategyRepository::findAll] 无parameters查询找到" << countWithoutParams << "个策略";
    } else {
        qWarning() << "[StrategyRepository::findAll] 无parameters查询失败:" << queryWithoutParams.lastError().text();
    }
    
    // 第三步：尝试将parameters字段转换为文本
    qDebug() << "[StrategyRepository::findAll] === 第三步：尝试将parameters字段转换为文本 ===";
    QSqlQuery queryWithTextParams(conn.get());
    const QString sqlWithTextParams = selectStrategiesWithParametersTextSql();
    
    if (queryWithTextParams.exec(sqlWithTextParams)) {
        int countWithTextParams = 0;
        while (queryWithTextParams.next()) {
            countWithTextParams++;
            QString id = queryWithTextParams.value("strategy_id").toString();
            QString paramsText = queryWithTextParams.value("parameters_text").toString();
            qDebug() << "[StrategyRepository::findAll] 文本parameters查询找到策略" << countWithTextParams << "ID:" << id << "参数长度:" << paramsText.length();
        }
        qDebug() << "[StrategyRepository::findAll] 文本parameters查询找到" << countWithTextParams << "个策略";
        
        // 如果这个查询成功，使用这个结果
        if (countWithTextParams > 0) {
            qDebug() << "[StrategyRepository::findAll] 使用文本参数查询结果";
            // 重新执行查询并处理
            queryWithTextParams.finish();
            if (queryWithTextParams.exec(sqlWithTextParams)) {
                while (queryWithTextParams.next()) {
                    QVariantMap strategy;

                    assignStrategyBaseFields(strategy, queryWithTextParams);
                    
                    // 解析parameters字段
                    QString parametersJson = queryWithTextParams.value("parameters_text").toString();
                    QVariantMap parameters;
                    QString parseError;
                    if (tryParseJsonObjectVariantMap(parametersJson, parameters, &parseError)) {
                        assignStrategyParameters(strategy, parameters);
                    } else {
                        qWarning() << "[StrategyRepository::findAll] 无法解析转换后的JSON参数:" << parseError;
                        assignStrategyParameters(strategy, QVariantMap());
                    }

                    assignPersistedStrategyIdentity(strategy);
                    
                    strategies.push_back(hydrateStrategyDataFromMap(strategy));
                    processedCount++;
                }
            }
        }
    } else {
        qWarning() << "[StrategyRepository::findAll] 文本parameters查询失败:" << queryWithTextParams.lastError().text();
    }
    
    // 如果前面的查询都没有结果，尝试不使用JSON字段的查询
    if (processedCount == 0) {
        qDebug() << "[StrategyRepository::findAll] === 使用两步查询法 ===";
        // 第一步：查询所有非JSON字段
        QSqlQuery baseQuery(conn.get());
        const QString baseSql = selectStrategiesWithoutParametersSql();
        
        if (baseQuery.exec(baseSql)) {
            while (baseQuery.next()) {
                QVariantMap strategy;

                assignStrategyBaseFields(strategy, baseQuery);
                
                // 第二步：单独查询JSON字段
                QString strategyId = baseQuery.value("strategy_id").toString();
                QSqlQuery paramQuery(conn.get());
                paramQuery.prepare("SELECT parameters FROM strategy WHERE strategy_id = ?");
                paramQuery.addBindValue(strategyId);
                
                if (paramQuery.exec() && paramQuery.next()) {
                    QString parametersJson = paramQuery.value("parameters").toString();
                    QVariantMap parameters;
                    QString parseError;
                    if (tryParseJsonObjectVariantMap(parametersJson, parameters, &parseError)) {
                        assignStrategyParameters(strategy, parameters);
                    } else {
                        qWarning() << "[StrategyRepository::findAll] 无法解析单独查询的JSON参数:" << parseError;
                        assignStrategyParameters(strategy, QVariantMap());
                    }
                } else {
                    assignStrategyParameters(strategy, QVariantMap());
                }

                assignPersistedStrategyIdentity(strategy);
                
                strategies.push_back(hydrateStrategyDataFromMap(strategy));
                processedCount++;
            }
            rowCount = processedCount;
        }
    }
    
    qDebug() << "[StrategyRepository::findAll] 查询完成，成功处理" << processedCount << "个策略";
    
    // 输出策略数量验证
    if (processedCount == 0) {
        qWarning() << "[StrategyRepository::findAll] 警告：成功处理0条记录，但数据库中有策略数据";
        // 再次验证查询结果
        QSqlQuery countQuery = QSqlQuery(conn.get());
        if (countQuery.exec("SELECT COUNT(*) FROM strategy") && countQuery.next()) {
            int dbCount = countQuery.value(0).toInt();
            qWarning() << "[StrategyRepository::findAll] 数据库实际策略数量:" << dbCount;
        }
    }
    
    return strategies;
}

std::vector<StrategyData> StrategyRepository::findByType(StrategyStoredType strategyType) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return {};
    }
    
    QSqlQuery query(conn.get());
    query.prepare(selectStrategiesWithoutParametersSql(
        QStringLiteral("WHERE strategy_type = ? ORDER BY created_at DESC")));
    if (!bindPersistedStoredType(query, strategyType)) {
        qWarning() << "[StrategyRepository] Invalid persisted strategy type" << static_cast<int>(strategyType);
        return {};
    }
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to find strategies by type:" << query.lastError().text();
        return {};
    }
    
    std::vector<StrategyData> strategies;
    while (query.next()) {
        QString strategyId = query.value("strategy_id").toString();
        strategies.push_back(hydrateStrategyDataWithParameters(rowToStrategyData(query),
                                                               loadStrategyParameters(strategyId, conn.get())));
    }
    
    return strategies;
}

std::vector<StrategyData> StrategyRepository::findByStatus(strategy_view::StrategyLifecycleStatus status) {
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
    
    std::vector<StrategyData> strategies;
    while (query.next()) {
        QString strategyId = query.value("strategy_id").toString();
        strategies.push_back(hydrateStrategyDataWithParameters(rowToStrategyData(query),
                                                               loadStrategyParameters(strategyId, conn.get())));
    }
    
    return strategies;
}

std::vector<StrategyData> StrategyRepository::search(const QString& keyword) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return {};
    }
    
    QSqlQuery query(conn.get());
    QString searchPattern = "%" + keyword + "%";
    query.prepare(selectStrategiesWithoutParametersSql(
        QStringLiteral("WHERE strategy_name LIKE ? OR strategy_code LIKE ? OR description LIKE ? ORDER BY created_at DESC")));
    query.addBindValue(searchPattern);
    query.addBindValue(searchPattern);
    query.addBindValue(searchPattern);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to search strategies:" << query.lastError().text();
        return {};
    }
    
    std::vector<StrategyData> strategies;
    while (query.next()) {
        QString strategyId = query.value("strategy_id").toString();
        strategies.push_back(hydrateStrategyDataWithParameters(rowToStrategyData(query),
                                                               loadStrategyParameters(strategyId, conn.get())));
    }
    
    return strategies;
}

QString StrategyRepository::save(const StrategyData& strategy) {
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

bool StrategyRepository::update(const QString& strategyId, const StrategyData& strategy) {
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
    
    QString resultId = saveStrategyInternal(StrategyData::fromVariantMap(updatedStrategy), db, true);
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
    
    // 清空参数表
    QSqlQuery clearParamsQuery(db);
    if (!clearParamsQuery.exec("DELETE FROM backtest_config WHERE config_id IN (SELECT config_id FROM backtest_config)")) {
        qWarning() << "[StrategyRepository] Failed to clear backtest config:" << clearParamsQuery.lastError().text();
        db.rollback();
        return false;
    }
    
    // 清空策略表
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

    StrategyData updatedStrategy = strategy.value();
    updatedStrategy.parameters = mergeVariantMapsRecursive(updatedStrategy.parameters, parameters);
    return update(strategyId, updatedStrategy);
}

bool StrategyRepository::updatePerformance(const QString& strategyId, const QVariantMap& performance) {
    const auto strategy = findById(strategyId);
    if (!strategy.has_value()) {
        return false;
    }

    StrategyData updatedStrategy = strategy.value();
    updatedStrategy.performanceMetrics = performance;
    return update(strategyId, updatedStrategy);
}

std::vector<StrategyData> StrategyRepository::findActiveStrategies() {
    return findByStatus(strategy_view::StrategyLifecycleStatus::Active);
}

std::vector<StrategyData> StrategyRepository::findDraftStrategies() {
    return {};
}

StrategyData StrategyRepository::rowToStrategyData(const QSqlQuery& query) {
    QVariantMap strategy;

    assignStrategyBaseFields(strategy, query);

    int performanceColumn = query.record().indexOf("performance_metrics");
    if (performanceColumn >= 0) {
        assignParsedJsonObjectField(strategy,
                                    persistedPerformanceMetricsKey(),
                                    query.value(performanceColumn).toString());
    }
    
    // 解析参数JSON
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

    assignPersistedStrategyIdentity(strategy);
    
    return StrategyData::fromVariantMap(strategy);
}

QVariantMap StrategyRepository::loadStrategyParameters(const QString& strategyId, QSqlDatabase& db) {
    // 策略参数存储在strategy表的parameters字段中（JSON格式）
    // 这个方法用于从JSON字段解析参数
    
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
    // 将参数转换为JSON字符串
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
    // 参数存储在strategy表的JSON字段中，更新为空JSON即可
    QSqlQuery query(db);
    query.prepare("UPDATE strategy SET parameters = '{}' WHERE strategy_id = ?");
    query.addBindValue(strategyId);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to delete strategy parameters:" << query.lastError().text();
        return false;
    }
    
    return true;
}

QString StrategyRepository::saveStrategyInternal(const StrategyData& strategy, QSqlDatabase& db, bool isUpdate) {
    const QVariantMap strategyMap = strategy.toVariantMap();
    QString strategyId = strategy.strategyId;
    QString strategyCode = strategy.strategyCode;
    QString strategyName = strategy.strategyName;
    QString description = strategy.description;
    QString version = strategy.version;
    QString author = strategy.author;
    const StrategyLanguageCode languageCode = strategy.language;
    const ResolvedStrategyIdentity strategyIdentity = strategy.strategyIdentity;
    const strategy_view::StrategyLifecycleStatus strategyStatus = strategy.status;
    const PersistedStrategyStatusCode statusCode = persistedStatusCodeFromLifecycle(strategyStatus);
    
    // 验证必填字段
    if (strategyName.isEmpty() || !strategyIdentity.validStoredType) {
        qWarning() << "[StrategyRepository] Strategy name and type are required";
        return QString();
    }
    if (statusCode == PersistedStrategyStatusCode::Unknown) {
        qWarning() << "[StrategyRepository] Strategy statusIndex is required and must be persistable";
        return QString();
    }
    
    // 生成策略代码（如果未提供）
    if (strategyCode.isEmpty() && !isUpdate) {
        strategyCode = generateStrategyCode(strategy);
    }
    
    QSqlQuery query(db);
    
    if (isUpdate) {
        query.prepare(QStringLiteral("UPDATE strategy SET %1, updated_at = NOW() WHERE strategy_id = ?")
                          .arg(persistedStrategyUpdateAssignmentsSql()));
        
        query.addBindValue(strategyCode);
        query.addBindValue(strategyName);
        if (!bindPersistedStoredType(query, strategyIdentity.storedType)) {
            qWarning() << "[StrategyRepository] Invalid persisted strategy type on update";
            return QString();
        }
        query.addBindValue(description);
        query.addBindValue(version);
        query.addBindValue(author);
        bindPersistedLanguageCode(query, languageCode);
        bindPersistedStatusCode(query, statusCode);
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
        
        // 如果 strategyId 为空，使用 strategyCode 作为默认ID
        if (strategyId.isEmpty()) {
            strategyId = strategyCode;  // 使用策略代码作为ID
        }
        
        // 插入包含 strategy_id 的记录
        query.prepare(QStringLiteral("INSERT INTO strategy (%1) VALUES (%2)")
                  .arg(persistedStrategyInsertColumnsSql(),
                       persistedStrategyInsertValuesSql()));
        
        query.addBindValue(strategyId);
        query.addBindValue(strategyCode);
        query.addBindValue(strategyName);
        if (!bindPersistedStoredType(query, strategyIdentity.storedType)) {
            qWarning() << "[StrategyRepository] Invalid persisted strategy type on insert";
            return QString();
        }
        query.addBindValue(description);
        query.addBindValue(version);
        query.addBindValue(author);
        bindPersistedLanguageCode(query, languageCode);
        bindPersistedStatusCode(query, statusCode);
    }
    
    if (!query.exec()) {
        QSqlError error = query.lastError();
        qWarning() << "[StrategyRepository] Failed to save strategy. Error:" << error.text() 
                   << " | SQL:" << query.lastQuery() 
                   << " | Bound values:" << query.boundValues();
        return QString();
    }
    
    // 对于INSERT操作，使用我们设置的strategyId
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

bool StrategyRepository::validateStrategy(const StrategyData& strategy) const {
    const QVariantMap strategyMap = strategy.toVariantMap();
    if (strategy.strategyName.isEmpty()) {
        qWarning() << "[StrategyRepository] Validation failed: strategy_name is required";
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

    if (!requireRuntimeIndexForRawText(strategyMap,
                                       legacyAssetTypeKey(),
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

    if (!requireRuntimeIndexForRawText(strategyMap,
                                       legacyTimeFrameKey(),
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

    if (!requireRuntimeIndexForRawText(strategyMap,
                                       legacyRiskLevelKey(),
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

QString StrategyRepository::generateStrategyCode(const StrategyData& strategy) const {
    QString name = strategy.strategyName;
    const ResolvedStrategyIdentity strategyIdentity = strategy.strategyIdentity;
    
    // 从名称生成简写代码
    QString codePrefix;
    switch (strategyIdentity.storedType) {
    case StrategyStoredType::TrendFollowing:
        codePrefix = "TRD_";
        break;
    case StrategyStoredType::MeanReversion:
        codePrefix = "MR_";
        break;
    case StrategyStoredType::Alpha:
        codePrefix = "ALPHA_";
        break;
    case StrategyStoredType::Arbitrage:
        codePrefix = "ARB_";
        break;
    case StrategyStoredType::Portfolio:
        codePrefix = "PTF_";
        break;
    case StrategyStoredType::HighFrequency:
        codePrefix = "HFT_";
        break;
    case StrategyStoredType::Custom:
    case StrategyStoredType::Unknown:
    default:
        codePrefix = "GEN_";
        break;
    }
    
    // 使用名称的前几个字符，转换为大写，移除空格
    QString namePart = name.left(10).toUpper().replace(" ", "_").replace("-", "_");
    
    // 添加毫秒级时间戳和随机尾缀，避免高频创建时撞唯一键
    QString timestamp = QDateTime::currentDateTimeUtc().toString("yyyyMMddHHmmsszzz");
    QString entropy = QString::number(QRandomGenerator::global()->bounded(1000, 10000));
    
    return codePrefix + namePart + "_" + timestamp + entropy;
}

} // namespace database
} // namespace astock