#include "TradingConnectionConfigService.h"
#include "../../ui/bridge/include/DatabaseConnectionManager.h"
#include "PositionAccountService.h"
#include "RiskMonitorService.h"
#include "database/StrategyRepository.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QMutexLocker>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSaveFile>

#include <cstdlib>
#include <thread>

using namespace astock::database;

namespace {

constexpr int kMaxStrategySegmentLength = 28;
constexpr int kMaxRuntimeStrategyLength = 60;

QString sanitizeStrategyIdValue(const QString& value, const QString& fallback, int maxLength)
{
    const QString lowered = value.trimmed().toLower();
    QString sanitized;
    sanitized.reserve(lowered.size());

    bool lastWasSeparator = false;
    for (const QChar ch : lowered) {
        if (ch.isLetterOrNumber()) {
            sanitized.append(ch);
            lastWasSeparator = false;
            continue;
        }

        if (!sanitized.isEmpty() && !lastWasSeparator) {
            sanitized.append(QChar('_'));
            lastWasSeparator = true;
        }
    }

    while (sanitized.endsWith(QChar('_'))) {
        sanitized.chop(1);
    }

    if (sanitized.isEmpty()) {
        sanitized = fallback;
    }

    if (maxLength > 0 && sanitized.size() > maxLength) {
        sanitized = sanitized.left(maxLength);
        while (sanitized.endsWith(QChar('_'))) {
            sanitized.chop(1);
        }
    }

    return sanitized;
}

QString sanitizeStrategyIdSegment(const QString& value, const QString& fallback)
{
    return sanitizeStrategyIdValue(value, fallback, kMaxStrategySegmentLength);
}

QString normalizeStrategyIdAlias(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    return sanitizeStrategyIdValue(trimmed, QString(), kMaxRuntimeStrategyLength);
}

QString readEnvironmentText(const char* name)
{
    if (const char* value = std::getenv(name)) {
        return QString::fromLocal8Bit(value).trimmed();
    }
    return {};
}

bool isPlaceholderAccountId(const QString& accountId)
{
    const QString normalized = accountId.trimmed().toUpper();
    return normalized.isEmpty() || normalized == QStringLiteral("SIM_ACCOUNT");
}

QString resolvedStartupGateToken(const QVariantMap& configuration)
{
    const QString configuredToken = configuration.value(QStringLiteral("token")).toString().trimmed();
    return configuredToken.isEmpty() ? readEnvironmentText("ASTOCK_GM_TOKEN") : configuredToken;
}

QString resolvedStartupGateAccountId(const QVariantMap& configuration)
{
    const QString configuredAccountId = configuration.value(QStringLiteral("accountId")).toString().trimmed();
    const QString envAccountId = readEnvironmentText("ASTOCK_GM_ACCOUNT_ID");
    if (!envAccountId.isEmpty()) {
        return envAccountId;
    }
    return isPlaceholderAccountId(configuredAccountId) ? QString() : configuredAccountId;
}

QVariantMap buildStartupGateResult(bool ready,
                                   const QString& ruleId,
                                   const QString& reasonCode,
                                   const QString& reason,
                                   const QVariantMap& checks)
{
    QVariantMap result;
    result.insert(QStringLiteral("ready"), ready);
    result.insert(QStringLiteral("passed"), ready);
    result.insert(QStringLiteral("stage"), QStringLiteral("Startup"));
    result.insert(QStringLiteral("stageCode"), QStringLiteral("startup"));
    result.insert(QStringLiteral("decisionType"), ready ? QStringLiteral("Pass") : QStringLiteral("Block"));
    result.insert(QStringLiteral("ruleId"), ruleId);
    result.insert(QStringLiteral("reasonCode"), reasonCode);
    result.insert(QStringLiteral("reason"), reason);
    result.insert(QStringLiteral("message"), reason);
    result.insert(QStringLiteral("checks"), checks);
    result.insert(QStringLiteral("checkedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return result;
}

QString buildRuntimeStrategyIdAlias(const QString& accountId, const QString& boundStrategyId)
{
    const QString sanitizedAccountId = sanitizeStrategyIdSegment(accountId, QStringLiteral("acct"));
    const QString sanitizedStrategyId = sanitizeStrategyIdSegment(boundStrategyId, QStringLiteral("strategy"));
    if (sanitizedAccountId.isEmpty() || sanitizedStrategyId.isEmpty()) {
        return {};
    }

    QString runtimeStrategyId = sanitizedAccountId + QChar('_') + sanitizedStrategyId;
    if (runtimeStrategyId.size() > kMaxRuntimeStrategyLength) {
        runtimeStrategyId = runtimeStrategyId.left(kMaxRuntimeStrategyLength);
        while (runtimeStrategyId.endsWith(QChar('_'))) {
            runtimeStrategyId.chop(1);
        }
    }
    return runtimeStrategyId;
}

QString buildAccountRuntimeStrategyIdAlias(const QString& accountId)
{
    const QString sanitizedAccountId = sanitizeStrategyIdSegment(accountId, QStringLiteral("acct"));
    const QString sanitizedScope = sanitizeStrategyIdSegment(QStringLiteral("account"), QStringLiteral("account"));
    if (sanitizedAccountId.isEmpty() || sanitizedScope.isEmpty()) {
        return {};
    }

    QString runtimeStrategyId = sanitizedAccountId + QChar('_') + sanitizedScope;
    if (runtimeStrategyId.size() > kMaxRuntimeStrategyLength) {
        runtimeStrategyId = runtimeStrategyId.left(kMaxRuntimeStrategyLength);
        while (runtimeStrategyId.endsWith(QChar('_'))) {
            runtimeStrategyId.chop(1);
        }
    }
    return runtimeStrategyId;
}

QVariantMap baseTradingConfiguration()
{
    QVariantMap config;
    config.insert(QStringLiteral("provider"), QStringLiteral("jujin"));
    config.insert(QStringLiteral("enabled"), false);
    config.insert(QStringLiteral("simtradeOnly"), false);
    config.insert(QStringLiteral("readOnly"), true);
    config.insert(QStringLiteral("liveUnlockConfirmed"), false);
    config.insert(QStringLiteral("liveUnlockAcknowledgedAt"), QString());
    config.insert(QStringLiteral("autoExecuteRuntimeCandidates"), false);
    config.insert(QStringLiteral("token"), QString());
    config.insert(QStringLiteral("accountProfile"), QStringLiteral("live"));
    config.insert(QStringLiteral("liveAccountId"), QString());
    config.insert(QStringLiteral("simAccountId"), QString());
    config.insert(QStringLiteral("accountId"), QString());
    config.insert(QStringLiteral("boundStrategyId"), QString());
    config.insert(QStringLiteral("boundStrategyName"), QString());
    config.insert(QStringLiteral("boundStrategies"), QVariantList{});
    config.insert(QStringLiteral("boundStrategyIds"), QStringList{});
    config.insert(QStringLiteral("accountRuntimeStrategyId"), QString());
    config.insert(QStringLiteral("gmStrategyId"), QString());
    config.insert(QStringLiteral("runtimeStrategyId"), QString());
    config.insert(QStringLiteral("strategyId"), QString());
    config.insert(QStringLiteral("mode"), QStringLiteral("1"));
    config.insert(QStringLiteral("serverUrl"), QString());
    config.insert(QStringLiteral("symbols"), QString());
    config.insert(
        QStringLiteral("clientProcessNames"),
        QStringList{
            QStringLiteral("myquant.exe"),
            QStringLiteral("MiniQmt.exe"),
            QStringLiteral("XtMiniQmt.exe"),
            QStringLiteral("gmtrade.exe"),
            QStringLiteral("掘金量化终端.exe"),
            QStringLiteral("ds-proxy.exe"),
            QStringLiteral("gmterm-serv.exe"),
            QStringLiteral("国投证券掘金.exe")
        });
    config.insert(QStringLiteral("updatedAt"), QString());
    return config;
}

QJsonObject toJsonObject(const QVariantMap& map)
{
    return QJsonObject::fromVariantMap(map);
}

QVariantMap toVariantMap(const QJsonObject& object)
{
    return object.toVariantMap();
}

QStringList normalizedSymbolList(const QVariant& value)
{
    QStringList rawSymbols;
    if (value.canConvert<QStringList>()) {
        rawSymbols = value.toStringList();
    } else if (value.typeId() == QMetaType::QVariantList) {
        const QVariantList symbolList = value.toList();
        for (const QVariant& symbolValue : symbolList) {
            const QString symbol = symbolValue.toString().trimmed();
            if (!symbol.isEmpty()) {
                rawSymbols.append(symbol);
            }
        }
    } else {
        rawSymbols = value.toString().split(QRegularExpression(QStringLiteral("[,;\\s，；]+")), Qt::SkipEmptyParts);
    }

    QStringList symbols;
    for (const QString& rawSymbol : rawSymbols) {
        const QString symbol = rawSymbol.trimmed().toUpper();
        if (!symbol.isEmpty() && !symbols.contains(symbol)) {
            symbols.append(symbol);
        }
    }
    return symbols;
}

QString normalizedSymbolText(const QVariant& value)
{
    return normalizedSymbolList(value).join(QStringLiteral(","));
}

QVariant firstConfiguredValue(const QVariantMap& map, const QStringList& keys)
{
    for (const QString& key : keys) {
        if (!map.contains(key)) {
            continue;
        }

        const QVariant value = map.value(key);
        if (!value.isValid() || value.isNull()) {
            continue;
        }

        if (value.typeId() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
            continue;
        }

        return value;
    }

    return {};
}

int enumParam(const QVariantMap& map, const QStringList& keys, int fallback)
{
    const QVariant rawValue = firstConfiguredValue(map, keys);
    if (!rawValue.isValid()) {
        return fallback;
    }

    bool ok = false;
    const int numericValue = rawValue.toInt(&ok);
    return ok ? numericValue : fallback;
}

QString strategyIdFromBindingEntry(const QVariantMap& entry)
{
    return entry.value(QStringLiteral("strategyId"),
        entry.value(QStringLiteral("strategy_id"), entry.value(QStringLiteral("id")))).toString().trimmed();
}

QString strategyNameFromBindingEntry(const QVariantMap& entry, const QString& fallbackId)
{
    const QString strategyName = entry.value(QStringLiteral("strategyName"),
        entry.value(QStringLiteral("strategy_name"), entry.value(QStringLiteral("name")))).toString().trimmed();
    return strategyName.isEmpty() ? fallbackId : strategyName;
}

void appendBoundStrategyEntry(QVariantList& target,
                              QSet<QString>& seenIds,
                              const QString& strategyId,
                              const QString& strategyName)
{
    const QString normalizedStrategyId = strategyId.trimmed();
    if (normalizedStrategyId.isEmpty() || seenIds.contains(normalizedStrategyId)) {
        return;
    }

    QVariantMap entry;
    entry.insert(QStringLiteral("strategyId"), normalizedStrategyId);
    entry.insert(QStringLiteral("strategyName"), strategyName.trimmed().isEmpty() ? normalizedStrategyId : strategyName.trimmed());
    target.append(entry);
    seenIds.insert(normalizedStrategyId);
}

void appendBoundStrategyVariant(QVariantList& target,
                                QSet<QString>& seenIds,
                                const QVariant& rawValue)
{
    if (!rawValue.isValid() || rawValue.isNull()) {
        return;
    }

    if (rawValue.canConvert<QVariantMap>()) {
        const QVariantMap entry = rawValue.toMap();
        const QString strategyId = strategyIdFromBindingEntry(entry);
        appendBoundStrategyEntry(target, seenIds, strategyId, strategyNameFromBindingEntry(entry, strategyId));
        return;
    }

    const QString strategyId = rawValue.toString().trimmed();
    appendBoundStrategyEntry(target, seenIds, strategyId, strategyId);
}

QVariantList normalizedBoundStrategyEntries(const QVariant& rawValue)
{
    QVariantList normalizedEntries;
    QSet<QString> seenIds;

    if (!rawValue.isValid() || rawValue.isNull()) {
        return normalizedEntries;
    }

    if (rawValue.canConvert<QVariantList>()) {
        const QVariantList values = rawValue.toList();
        for (const QVariant& value : values) {
            appendBoundStrategyVariant(normalizedEntries, seenIds, value);
        }
        return normalizedEntries;
    }

    if (rawValue.canConvert<QStringList>()) {
        const QStringList values = rawValue.toStringList();
        for (const QString& value : values) {
            appendBoundStrategyEntry(normalizedEntries, seenIds, value, value);
        }
        return normalizedEntries;
    }

    const QStringList values = rawValue.toString().split(QRegularExpression(QStringLiteral("[,;\\s，；]+")), Qt::SkipEmptyParts);
    for (const QString& value : values) {
        appendBoundStrategyEntry(normalizedEntries, seenIds, value, value);
    }

    return normalizedEntries;
}

QVariantList ensurePrimaryBoundStrategy(const QVariantList& rawEntries,
                                       const QString& primaryStrategyId,
                                       const QString& primaryStrategyName)
{
    QVariantList orderedEntries;
    QSet<QString> seenIds;

    appendBoundStrategyEntry(orderedEntries, seenIds, primaryStrategyId, primaryStrategyName);
    for (const QVariant& rawEntry : rawEntries) {
        appendBoundStrategyVariant(orderedEntries, seenIds, rawEntry);
    }

    return orderedEntries;
}

QVariantList mergedBoundStrategyEntries(const QVariantMap& rawConfiguration,
                                        const QVariantMap& fallbackConfiguration)
{
    const QVariant rawEntries = rawConfiguration.contains(QStringLiteral("boundStrategies"))
        ? rawConfiguration.value(QStringLiteral("boundStrategies"))
        : fallbackConfiguration.value(QStringLiteral("boundStrategies"));
    const QVariantList entries = normalizedBoundStrategyEntries(rawEntries);

    QString primaryStrategyId = rawConfiguration.value(QStringLiteral("boundStrategyId")).toString().trimmed();
    QString primaryStrategyName = rawConfiguration.value(QStringLiteral("boundStrategyName")).toString().trimmed();
    if (primaryStrategyId.isEmpty()) {
        primaryStrategyId = fallbackConfiguration.value(QStringLiteral("boundStrategyId")).toString().trimmed();
        if (primaryStrategyName.isEmpty()) {
            primaryStrategyName = fallbackConfiguration.value(QStringLiteral("boundStrategyName")).toString().trimmed();
        }
    }

    return ensurePrimaryBoundStrategy(entries, primaryStrategyId, primaryStrategyName);
}

QStringList boundStrategyIds(const QVariantList& boundStrategies)
{
    QStringList strategyIds;
    for (const QVariant& rawEntry : boundStrategies) {
        const QString strategyId = strategyIdFromBindingEntry(rawEntry.toMap());
        if (!strategyId.isEmpty() && !strategyIds.contains(strategyId)) {
            strategyIds.append(strategyId);
        }
    }
    return strategyIds;
}

QStringList loadCurrentHoldingSymbols()
{
    PositionAccountService* positionAccountService = PositionAccountService::instance();
    if (!positionAccountService) {
        return {};
    }

    QStringList symbols;
    const QVariantList positions = positionAccountService->positions();
    for (const QVariant& rawPosition : positions) {
        const QVariantMap position = rawPosition.toMap();
        const QString symbol = position.value(QStringLiteral("symbol")).toString().trimmed().toUpper();
        if (!symbol.isEmpty() && !symbols.contains(symbol)) {
            symbols.append(symbol);
        }
    }
    return symbols;
}

QStringList mergedRuntimeSubscriptionSymbols(const QStringList& primarySymbols,
                                            const QStringList& secondarySymbols)
{
    QStringList symbols;
    for (const QString& symbol : primarySymbols) {
        if (!symbol.isEmpty() && !symbols.contains(symbol)) {
            symbols.append(symbol);
        }
    }
    for (const QString& symbol : secondarySymbols) {
        if (!symbol.isEmpty() && !symbols.contains(symbol)) {
            symbols.append(symbol);
        }
    }
    return symbols;
}

} // namespace

TradingConnectionConfigService* TradingConnectionConfigService::m_instance = nullptr;
QMutex TradingConnectionConfigService::m_instanceMutex;

TradingConnectionConfigService* TradingConnectionConfigService::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new TradingConnectionConfigService();
        m_instance->initialize();
    }
    return m_instance;
}

TradingConnectionConfigService::TradingConnectionConfigService(QObject* parent)
    : QObject(parent)
    , m_initialized(false)
    , m_clientProcessRunning(false)
    , m_clientProcessStatus(QStringLiteral("尚未检查掘金客户端进程"))
    , m_currentConfiguration(baseTradingConfiguration())
{
}

void TradingConnectionConfigService::initialize()
{
    bool configurationChanged = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_initialized) {
            return;
        }

        loadPersistedState();
        m_initialized = true;
        configurationChanged = true;
    }

    refreshClientProcessStatus();
    if (configurationChanged) {
        emit currentConfigurationChanged();
    }
    emit initializedChanged();
}

void TradingConnectionConfigService::initializeAsync()
{
    QPointer<TradingConnectionConfigService> safeService(this);
    std::thread([safeService]() {
        if (safeService) {
            safeService->initialize();
        }
    }).detach();
}

QVariantMap TradingConnectionConfigService::loadConfiguration()
{
    bool needsInit = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_initialized) {
            loadPersistedState();
            m_initialized = true;
            needsInit = true;
        }
    }

    if (needsInit) {
        refreshClientProcessStatus();
        emit initializedChanged();
    }

    QMutexLocker locker(&m_mutex);
    return m_currentConfiguration;
}

QVariantMap TradingConnectionConfigService::defaultConfiguration() const
{
    return baseTradingConfiguration();
}

QVariantMap TradingConnectionConfigService::evaluateStartupGate(bool requireClientProcess) const
{
    QMutexLocker locker(&m_mutex);
    return evaluateStartupGateLocked(requireClientProcess);
}

QStringList TradingConnectionConfigService::defaultClientProcessNames() const
{
    return baseTradingConfiguration().value(QStringLiteral("clientProcessNames")).toStringList();
}

bool TradingConnectionConfigService::saveConfiguration(const QVariantMap& configuration)
{
    QVariantMap savedConfiguration;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_initialized) {
            loadPersistedState();
            m_initialized = true;
        }

        const QVariantMap previousConfiguration = m_currentConfiguration;
        m_currentConfiguration = normalizedConfiguration(configuration);
        const bool liveIdentityChanged = previousConfiguration.value(QStringLiteral("accountId")).toString().trimmed()
                != m_currentConfiguration.value(QStringLiteral("accountId")).toString().trimmed()
            || previousConfiguration.value(QStringLiteral("boundStrategyId")).toString().trimmed()
                != m_currentConfiguration.value(QStringLiteral("boundStrategyId")).toString().trimmed()
            || previousConfiguration.value(QStringLiteral("accountRuntimeStrategyId")).toString().trimmed()
                != m_currentConfiguration.value(QStringLiteral("accountRuntimeStrategyId")).toString().trimmed()
            || previousConfiguration.value(QStringLiteral("runtimeStrategyId")).toString().trimmed()
                != m_currentConfiguration.value(QStringLiteral("runtimeStrategyId")).toString().trimmed()
            || previousConfiguration.value(QStringLiteral("gmStrategyId")).toString().trimmed()
                != m_currentConfiguration.value(QStringLiteral("gmStrategyId")).toString().trimmed();
        const bool liveModeActive = m_currentConfiguration.value(QStringLiteral("enabled")).toBool()
            && !m_currentConfiguration.value(QStringLiteral("readOnly"), true).toBool();
        bool liveUnlockConfirmed = m_currentConfiguration.value(
            QStringLiteral("liveUnlockConfirmed"),
            previousConfiguration.value(QStringLiteral("liveUnlockConfirmed"), false)).toBool();
        QString liveUnlockAcknowledgedAt = m_currentConfiguration.value(
            QStringLiteral("liveUnlockAcknowledgedAt"),
            previousConfiguration.value(QStringLiteral("liveUnlockAcknowledgedAt"))).toString().trimmed();
        if (!liveModeActive || liveIdentityChanged) {
            liveUnlockConfirmed = false;
            liveUnlockAcknowledgedAt.clear();
        } else if (liveUnlockConfirmed && liveUnlockAcknowledgedAt.isEmpty()) {
            liveUnlockAcknowledgedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        } else if (!liveUnlockConfirmed) {
            liveUnlockAcknowledgedAt.clear();
        }
        m_currentConfiguration.insert(QStringLiteral("liveUnlockConfirmed"), liveUnlockConfirmed);
        m_currentConfiguration.insert(QStringLiteral("liveUnlockAcknowledgedAt"), liveUnlockAcknowledgedAt);
        if (m_currentConfiguration.value(QStringLiteral("enabled")).toBool()
            && m_currentConfiguration.value(QStringLiteral("accountRuntimeStrategyId")).toString().trimmed().isEmpty()) {
            emit errorOccurred(QStringLiteral("启用掘金连接前未生成账户级运行时会话 ID"));
            return false;
        }
        if (m_currentConfiguration.value(QStringLiteral("enabled")).toBool()
            && m_currentConfiguration.value(QStringLiteral("accountId")).toString().trimmed().isEmpty()) {
            emit errorOccurred(QStringLiteral("启用掘金连接前必须填写当前账户环境对应的账户 ID"));
            return false;
        }
        m_currentConfiguration.insert(
            QStringLiteral("updatedAt"),
            QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

        if (!persistState()) {
            emit errorOccurred(QStringLiteral("交易连接配置保存失败"));
            return false;
        }

        savedConfiguration = m_currentConfiguration;
    }

    emit currentConfigurationChanged();
    emit configurationSaved(savedConfiguration);
    return true;
}

QVariantMap TradingConnectionConfigService::bindStrategyConfiguration(const QString& strategyId,
                                                                     const QString& strategyName,
                                                                     bool enableTrading,
                                                                     bool readOnly)
{
    const QString normalizedStrategyId = strategyId.trimmed();
    const QString normalizedStrategyName = strategyName.trimmed().isEmpty()
        ? normalizedStrategyId
        : strategyName.trimmed();
    if (normalizedStrategyId.isEmpty()) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), QStringLiteral("策略 ID 为空，无法更新交易绑定")}};
    }

    QVariantMap configuration = loadConfiguration();
    QVariantList boundStrategies = normalizedBoundStrategyEntries(configuration.value(QStringLiteral("boundStrategies")));
    boundStrategies.append(QVariantMap{{QStringLiteral("strategyId"), normalizedStrategyId},
                                       {QStringLiteral("strategyName"), normalizedStrategyName}});

    const QString primaryStrategyId = normalizedStrategyId;
    const QString primaryStrategyName = normalizedStrategyName;

    configuration.insert(QStringLiteral("boundStrategyId"), primaryStrategyId);
    configuration.insert(QStringLiteral("boundStrategyName"), primaryStrategyName);
    configuration.insert(QStringLiteral("boundStrategies"), ensurePrimaryBoundStrategy(boundStrategies, primaryStrategyId, primaryStrategyName));
    configuration.insert(QStringLiteral("readOnly"), readOnly);

    const QString accountId = configuration.value(QStringLiteral("accountId")).toString().trimmed();
    const bool hasAccountId = !accountId.isEmpty();
    const bool hasToken = !configuration.value(QStringLiteral("token")).toString().trimmed().isEmpty();
    const QString accountRuntimeStrategyId = hasAccountId
        ? buildAccountRuntimeStrategyIdAlias(accountId)
        : QString();
    const QString runtimeStrategyId = hasAccountId
        ? buildRuntimeStrategyIdAlias(accountId, primaryStrategyId)
        : QString();

    configuration.insert(QStringLiteral("accountRuntimeStrategyId"), accountRuntimeStrategyId);
    configuration.insert(QStringLiteral("gmStrategyId"), runtimeStrategyId);
    configuration.insert(QStringLiteral("runtimeStrategyId"), runtimeStrategyId);
    configuration.insert(QStringLiteral("strategyId"), runtimeStrategyId);

    if (enableTrading && hasAccountId) {
        configuration.insert(QStringLiteral("enabled"), true);
    } else if (enableTrading && !hasAccountId) {
        configuration.insert(QStringLiteral("enabled"), false);
    }

    if (!saveConfiguration(configuration)) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), QStringLiteral("交易配置保存失败，未能切换到当前策略")}};
    }

    const QVariantMap savedConfiguration = currentConfiguration();
    const QVariantMap startupGate = evaluateStartupGate(false);
    const bool tradingEnabled = savedConfiguration.value(QStringLiteral("enabled")).toBool();
    const bool readOnlyMode = savedConfiguration.value(QStringLiteral("readOnly"), true).toBool();
    const bool liveUnlockConfirmed = savedConfiguration.value(QStringLiteral("liveUnlockConfirmed"), false).toBool();
    const bool readyForTrading = startupGate.value(QStringLiteral("ready")).toBool();

    QString message = QStringLiteral("已切换当前交易绑定到策略: %1").arg(normalizedStrategyName);
    if (enableTrading && !hasAccountId) {
        message = QStringLiteral("已绑定当前策略，但交易账户未配置，暂未启用实盘连接");
    } else if (tradingEnabled && readOnlyMode) {
        message = QStringLiteral("已绑定当前策略，但当前仍处于只读模式");
    } else if (tradingEnabled && !liveUnlockConfirmed) {
        message = QStringLiteral("已绑定当前策略，但尚未显式解锁实盘提交");
    } else if (tradingEnabled && !hasToken) {
        message = QStringLiteral("已绑定当前策略，但 token 为空，暂时无法发起真实交易");
    } else if (readyForTrading) {
        message = QStringLiteral("已绑定当前策略并切换到可交易配置");
    }

    return QVariantMap{{QStringLiteral("success"), true},
                       {QStringLiteral("message"), message},
                       {QStringLiteral("configuration"), savedConfiguration},
                       {QStringLiteral("startupGate"), startupGate},
                       {QStringLiteral("readyForTrading"), readyForTrading},
                       {QStringLiteral("enabled"), tradingEnabled},
                       {QStringLiteral("readOnly"), readOnlyMode},
                       {QStringLiteral("runtimeStrategyId"), savedConfiguration.value(QStringLiteral("runtimeStrategyId"))}};
}

QVariantMap TradingConnectionConfigService::addBoundStrategyConfiguration(const QString& strategyId,
                                                                         const QString& strategyName,
                                                                         bool enableTrading,
                                                                         bool readOnly)
{
    const QString normalizedStrategyId = strategyId.trimmed();
    const QString normalizedStrategyName = strategyName.trimmed().isEmpty()
        ? normalizedStrategyId
        : strategyName.trimmed();
    if (normalizedStrategyId.isEmpty()) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), QStringLiteral("策略 ID 为空，无法加入交易绑定")}};
    }

    QVariantMap configuration = loadConfiguration();
    QVariantList boundStrategies = normalizedBoundStrategyEntries(configuration.value(QStringLiteral("boundStrategies")));
    boundStrategies.append(QVariantMap{{QStringLiteral("strategyId"), normalizedStrategyId},
                                       {QStringLiteral("strategyName"), normalizedStrategyName}});

    QString primaryStrategyId = configuration.value(QStringLiteral("boundStrategyId")).toString().trimmed();
    QString primaryStrategyName = configuration.value(QStringLiteral("boundStrategyName")).toString().trimmed();
    if (primaryStrategyId.isEmpty()) {
        primaryStrategyId = normalizedStrategyId;
        primaryStrategyName = normalizedStrategyName;
    }
    if (primaryStrategyName.isEmpty()) {
        primaryStrategyName = primaryStrategyId;
    }

    configuration.insert(QStringLiteral("boundStrategyId"), primaryStrategyId);
    configuration.insert(QStringLiteral("boundStrategyName"), primaryStrategyName);
    configuration.insert(QStringLiteral("boundStrategies"), ensurePrimaryBoundStrategy(boundStrategies, primaryStrategyId, primaryStrategyName));
    configuration.insert(QStringLiteral("readOnly"), readOnly);

    const QString accountId = configuration.value(QStringLiteral("accountId")).toString().trimmed();
    const bool hasAccountId = !accountId.isEmpty();
    const bool hasToken = !configuration.value(QStringLiteral("token")).toString().trimmed().isEmpty();
    if (configuration.value(QStringLiteral("accountRuntimeStrategyId")).toString().trimmed().isEmpty()
        && hasAccountId) {
        configuration.insert(QStringLiteral("accountRuntimeStrategyId"),
                             buildAccountRuntimeStrategyIdAlias(accountId));
    }
    if (configuration.value(QStringLiteral("gmStrategyId")).toString().trimmed().isEmpty()
        && configuration.value(QStringLiteral("runtimeStrategyId")).toString().trimmed().isEmpty()
        && configuration.value(QStringLiteral("strategyId")).toString().trimmed().isEmpty()
        && hasAccountId
        && !primaryStrategyId.isEmpty()) {
        const QString runtimeStrategyId = buildRuntimeStrategyIdAlias(accountId, primaryStrategyId);
        configuration.insert(QStringLiteral("gmStrategyId"), runtimeStrategyId);
        configuration.insert(QStringLiteral("runtimeStrategyId"), runtimeStrategyId);
        configuration.insert(QStringLiteral("strategyId"), runtimeStrategyId);
    }

    if (enableTrading && hasAccountId) {
        configuration.insert(QStringLiteral("enabled"), true);
    } else if (enableTrading && !hasAccountId) {
        configuration.insert(QStringLiteral("enabled"), false);
    }

    if (!saveConfiguration(configuration)) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), QStringLiteral("交易配置保存失败，未能追加运行策略")}};
    }

    const QVariantMap savedConfiguration = currentConfiguration();
    const QVariantMap startupGate = evaluateStartupGate(false);
    const bool tradingEnabled = savedConfiguration.value(QStringLiteral("enabled")).toBool();
    const bool readOnlyMode = savedConfiguration.value(QStringLiteral("readOnly"), true).toBool();
    const bool liveUnlockConfirmed = savedConfiguration.value(QStringLiteral("liveUnlockConfirmed"), false).toBool();
    const bool readyForTrading = startupGate.value(QStringLiteral("ready")).toBool();

    QString message = QStringLiteral("已将策略加入当前交易绑定: %1").arg(normalizedStrategyName);
    if (enableTrading && !hasAccountId) {
        message = QStringLiteral("已加入运行策略列表，但交易账户未配置，暂未启用实盘连接");
    } else if (tradingEnabled && readOnlyMode) {
        message = QStringLiteral("已加入运行策略列表，但当前仍处于只读模式");
    } else if (tradingEnabled && !liveUnlockConfirmed) {
        message = QStringLiteral("已加入运行策略列表，但尚未显式解锁实盘提交");
    } else if (tradingEnabled && !hasToken) {
        message = QStringLiteral("已加入运行策略列表，但 token 为空，暂时无法发起真实交易");
    } else if (readyForTrading) {
        message = QStringLiteral("已加入运行策略列表并保持实盘连接可用");
    }

    return QVariantMap{{QStringLiteral("success"), true},
                       {QStringLiteral("message"), message},
                       {QStringLiteral("configuration"), savedConfiguration},
                       {QStringLiteral("startupGate"), startupGate},
                       {QStringLiteral("readyForTrading"), readyForTrading},
                       {QStringLiteral("enabled"), tradingEnabled},
                       {QStringLiteral("readOnly"), readOnlyMode},
                       {QStringLiteral("runtimeStrategyId"), savedConfiguration.value(QStringLiteral("runtimeStrategyId"))}};
}

QVariantMap TradingConnectionConfigService::evaluateStartupGateLocked(bool requireClientProcess) const
{
    const QVariantMap& configuration = m_currentConfiguration;

    QVariantMap checks;
    checks.insert(QStringLiteral("marketConnectorCompiled"), marketConnectorCompiled());
    checks.insert(QStringLiteral("enabled"), configuration.value(QStringLiteral("enabled")).toBool());
    checks.insert(QStringLiteral("readOnly"), configuration.value(QStringLiteral("readOnly"), true).toBool());
    checks.insert(QStringLiteral("liveUnlockConfirmed"), configuration.value(QStringLiteral("liveUnlockConfirmed"), false).toBool());
    checks.insert(QStringLiteral("tokenPresent"), !resolvedStartupGateToken(configuration).isEmpty());
    checks.insert(QStringLiteral("accountBound"), !resolvedStartupGateAccountId(configuration).isEmpty());
    checks.insert(QStringLiteral("boundStrategyPresent"), !configuration.value(QStringLiteral("boundStrategyId")).toString().trimmed().isEmpty());
    checks.insert(QStringLiteral("accountRuntimePresent"), !configuration.value(QStringLiteral("accountRuntimeStrategyId")).toString().trimmed().isEmpty());
    checks.insert(QStringLiteral("runtimeStrategyPresent"), !configuration.value(QStringLiteral("runtimeStrategyId")).toString().trimmed().isEmpty());
    checks.insert(QStringLiteral("clientProcessRequired"), requireClientProcess);
    checks.insert(QStringLiteral("clientProcessRunning"), m_clientProcessRunning);

    if (!checks.value(QStringLiteral("marketConnectorCompiled")).toBool()) {
        return buildStartupGateResult(false,
                                      QStringLiteral("ConnectorCompiledRule"),
                                      QStringLiteral("market_connector_not_compiled"),
                                      marketConnectorBuildStatus(),
                                      checks);
    }

    if (!checks.value(QStringLiteral("enabled")).toBool()) {
        return buildStartupGateResult(false,
                                      QStringLiteral("TradingConnectionEnabledRule"),
                                      QStringLiteral("trading_connection_disabled"),
                                      QStringLiteral("Jujin trading connection is disabled"),
                                      checks);
    }

    if (checks.value(QStringLiteral("readOnly")).toBool()) {
        return buildStartupGateResult(false,
                                      QStringLiteral("ReadOnlyGateRule"),
                                      QStringLiteral("read_only_mode"),
                                      QStringLiteral("Trading connection is in read-only mode"),
                                      checks);
    }

    if (!checks.value(QStringLiteral("liveUnlockConfirmed")).toBool()) {
        return buildStartupGateResult(false,
                                      QStringLiteral("ExplicitLiveUnlockRule"),
                                      QStringLiteral("explicit_live_unlock_required"),
                                      QStringLiteral("Trading connection requires explicit live unlock"),
                                      checks);
    }

    if (!checks.value(QStringLiteral("tokenPresent")).toBool()) {
        return buildStartupGateResult(false,
                                      QStringLiteral("TokenPresentRule"),
                                      QStringLiteral("token_missing"),
                                      QStringLiteral("Jujin token is empty"),
                                      checks);
    }

    if (!checks.value(QStringLiteral("accountBound")).toBool()) {
        return buildStartupGateResult(false,
                                      QStringLiteral("AccountBoundRule"),
                                      QStringLiteral("account_missing"),
                                      QStringLiteral("Trading account is not configured"),
                                      checks);
    }

    if (!checks.value(QStringLiteral("accountRuntimePresent")).toBool()) {
        return buildStartupGateResult(false,
                                      QStringLiteral("AccountRuntimeIdentityRule"),
                                      QStringLiteral("account_runtime_missing"),
                                      QStringLiteral("Trading connection has no account runtime identity"),
                                      checks);
    }

    if (requireClientProcess && !checks.value(QStringLiteral("clientProcessRunning")).toBool()) {
        return buildStartupGateResult(false,
                                      QStringLiteral("ClientProcessAliveRule"),
                                      QStringLiteral("client_process_missing"),
                                      QStringLiteral("Jujin client process is not running"),
                                      checks);
    }

    return buildStartupGateResult(true,
                                  QStringLiteral("StartupGateChain"),
                                  QStringLiteral("startup_gate_passed"),
                                  QStringLiteral("Startup gate passed"),
                                  checks);
}

QVariantMap TradingConnectionConfigService::removeBoundStrategyConfiguration(const QString& strategyId)
{
    const QString normalizedStrategyId = strategyId.trimmed();
    if (normalizedStrategyId.isEmpty()) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), QStringLiteral("策略 ID 为空，无法移除交易绑定")}};
    }

    QVariantMap configuration = loadConfiguration();
    const QVariantList boundStrategies = normalizedBoundStrategyEntries(configuration.value(QStringLiteral("boundStrategies")));
    QVariantList remainingStrategies;
    QString removedStrategyName;
    for (const QVariant& rawEntry : boundStrategies) {
        const QVariantMap entry = rawEntry.toMap();
        const QString currentStrategyId = strategyIdFromBindingEntry(entry);
        if (currentStrategyId == normalizedStrategyId) {
            if (removedStrategyName.isEmpty()) {
                removedStrategyName = strategyNameFromBindingEntry(entry, currentStrategyId);
            }
            continue;
        }
        remainingStrategies.append(entry);
    }

    if (remainingStrategies.size() == boundStrategies.size()) {
        return QVariantMap{{QStringLiteral("success"), true},
                           {QStringLiteral("message"), QStringLiteral("当前策略未出现在交易绑定列表中")},
                           {QStringLiteral("configuration"), currentConfiguration()}};
    }

    QString primaryStrategyId = configuration.value(QStringLiteral("boundStrategyId")).toString().trimmed();
    QString primaryStrategyName = configuration.value(QStringLiteral("boundStrategyName")).toString().trimmed();
    if (primaryStrategyId == normalizedStrategyId) {
        primaryStrategyId.clear();
        primaryStrategyName.clear();
    }

    if (primaryStrategyId.isEmpty() && !remainingStrategies.isEmpty()) {
        const QVariantMap nextPrimary = remainingStrategies.first().toMap();
        primaryStrategyId = strategyIdFromBindingEntry(nextPrimary);
        primaryStrategyName = strategyNameFromBindingEntry(nextPrimary, primaryStrategyId);
    }

    configuration.insert(QStringLiteral("boundStrategyId"), primaryStrategyId);
    configuration.insert(QStringLiteral("boundStrategyName"), primaryStrategyName);
    configuration.insert(QStringLiteral("boundStrategies"), ensurePrimaryBoundStrategy(remainingStrategies, primaryStrategyId, primaryStrategyName));

    if (!saveConfiguration(configuration)) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), QStringLiteral("交易配置保存失败，未能移除运行策略")}};
    }

    const QVariantMap savedConfiguration = currentConfiguration();
    const QString displayName = removedStrategyName.isEmpty() ? normalizedStrategyId : removedStrategyName;
    return QVariantMap{{QStringLiteral("success"), true},
                       {QStringLiteral("message"), QStringLiteral("已将策略移出当前交易绑定: %1").arg(displayName)},
                       {QStringLiteral("configuration"), savedConfiguration},
                       {QStringLiteral("runtimeStrategyId"), savedConfiguration.value(QStringLiteral("runtimeStrategyId"))}};
}

void TradingConnectionConfigService::refreshClientProcessStatus()
{
    QString matchedProcessName;
    bool running = false;
    {
        QMutexLocker locker(&m_mutex);
        running = detectClientProcessLocked(&matchedProcessName);
        m_clientProcessRunning = running;
        m_clientProcessStatus = running
            ? QStringLiteral("已检测到客户端进程: %1").arg(matchedProcessName)
            : QStringLiteral("未检测到掘金客户端进程，接口当前不可用");
    }

    emit clientProcessStatusChanged();
}

void TradingConnectionConfigService::refreshClientProcessStatusAsync()
{
    QPointer<TradingConnectionConfigService> safeService(this);
    std::thread([safeService]() {
        if (safeService) {
            safeService->refreshClientProcessStatus();
        }
    }).detach();
}

bool TradingConnectionConfigService::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}

QVariantMap TradingConnectionConfigService::currentConfiguration() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentConfiguration;
}

QString TradingConnectionConfigService::configFilePath() const
{
    const QString baseDir = QCoreApplication::applicationDirPath();
    return QDir(baseDir).filePath(QStringLiteral("config/trading_connection.json"));
}

bool TradingConnectionConfigService::hasToken() const
{
    QMutexLocker locker(&m_mutex);
    return !m_currentConfiguration.value(QStringLiteral("token")).toString().trimmed().isEmpty();
}

bool TradingConnectionConfigService::marketConnectorCompiled() const
{
#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
    return true;
#else
    return false;
#endif
}

QString TradingConnectionConfigService::marketConnectorBuildStatus() const
{
#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
    return QStringLiteral("当前构建已编入掘金实时连接器，可继续校验 token、客户端进程和 SDK 运行条件");
#else
    return QStringLiteral("当前构建未编入掘金实时连接器。若要启用真实连接，需要重新以 ENABLE_JUJIN_MARKET=ON 构建，并提供 GMSDK_LIBRARY");
#endif
}

bool TradingConnectionConfigService::clientProcessRunning() const
{
    QMutexLocker locker(&m_mutex);
    return m_clientProcessRunning;
}

QString TradingConnectionConfigService::clientProcessStatus() const
{
    QMutexLocker locker(&m_mutex);
    return m_clientProcessStatus;
}

QStringList TradingConnectionConfigService::clientProcessNames() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentConfiguration.value(QStringLiteral("clientProcessNames")).toStringList();
}

void TradingConnectionConfigService::loadPersistedState()
{
    m_currentConfiguration = baseTradingConfiguration();

    QFile file(configFilePath());
    if (!file.exists()) {
        m_currentConfiguration.insert(
            QStringLiteral("updatedAt"),
            QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!persistState()) {
            emit errorOccurred(QStringLiteral("默认交易连接配置创建失败"));
        }
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(QStringLiteral("无法读取交易连接配置文件"));
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject()) {
        emit errorOccurred(QStringLiteral("交易连接配置文件格式无效"));
        return;
    }

    m_currentConfiguration = normalizedConfiguration(toVariantMap(document.object()));
    persistState();
}

bool TradingConnectionConfigService::persistState()
{
    const QString path = configFilePath();
    QFileInfo fileInfo(path);
    QDir dir = fileInfo.dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        emit errorOccurred(QStringLiteral("无法创建交易连接配置目录"));
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred(QStringLiteral("无法写入交易连接配置文件"));
        return false;
    }

    const QJsonDocument document(toJsonObject(m_currentConfiguration));
    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        emit errorOccurred(QStringLiteral("交易连接配置文件提交失败"));
        return false;
    }

    return true;
}

QVariantMap TradingConnectionConfigService::normalizedConfiguration(const QVariantMap& rawConfiguration) const
{
    QVariantMap normalized = baseTradingConfiguration();
    for (auto it = rawConfiguration.constBegin(); it != rawConfiguration.constEnd(); ++it) {
        normalized.insert(it.key(), it.value());
    }

    normalized.insert(QStringLiteral("provider"), QStringLiteral("jujin"));
    normalized.insert(QStringLiteral("enabled"), normalized.value(QStringLiteral("enabled")).toBool());
    normalized.insert(QStringLiteral("readOnly"), normalized.value(QStringLiteral("readOnly"), true).toBool());
    normalized.insert(QStringLiteral("liveUnlockConfirmed"),
                      normalized.value(QStringLiteral("liveUnlockConfirmed"), false).toBool());
    normalized.insert(QStringLiteral("liveUnlockAcknowledgedAt"),
                      normalized.value(QStringLiteral("liveUnlockAcknowledgedAt")).toString().trimmed());
    normalized.insert(QStringLiteral("autoExecuteRuntimeCandidates"),
                      normalized.value(QStringLiteral("autoExecuteRuntimeCandidates"), false).toBool());
    normalized.insert(QStringLiteral("token"), normalized.value(QStringLiteral("token")).toString().trimmed());
    const QString legacyAccountId = normalized.value(QStringLiteral("accountId")).toString().trimmed();
    QString accountProfile = normalized.value(QStringLiteral("accountProfile")).toString().trimmed().toLower();
    if (accountProfile != QStringLiteral("simulation")) {
        accountProfile = QStringLiteral("live");
    }
    QString liveAccountId = normalized.value(QStringLiteral("liveAccountId")).toString().trimmed();
    QString simAccountId = normalized.value(QStringLiteral("simAccountId")).toString().trimmed();
    if (liveAccountId.isEmpty() && simAccountId.isEmpty() && !legacyAccountId.isEmpty()) {
        if (accountProfile == QStringLiteral("simulation")) {
            simAccountId = legacyAccountId;
        } else {
            liveAccountId = legacyAccountId;
        }
    }

    const QString accountId = accountProfile == QStringLiteral("simulation") ? simAccountId : liveAccountId;
    const QVariantList normalizedBoundStrategies = mergedBoundStrategyEntries(rawConfiguration, m_currentConfiguration);
    const QStringList normalizedBoundStrategyIds = boundStrategyIds(normalizedBoundStrategies);

    QString boundStrategyId;
    QString boundStrategyName;
    if (!normalizedBoundStrategies.isEmpty()) {
        const QVariantMap primaryBoundStrategy = normalizedBoundStrategies.first().toMap();
        boundStrategyId = strategyIdFromBindingEntry(primaryBoundStrategy);
        boundStrategyName = strategyNameFromBindingEntry(primaryBoundStrategy, boundStrategyId);
    }

    normalized.insert(QStringLiteral("simtradeOnly"), false);
    normalized.insert(QStringLiteral("accountProfile"), accountProfile);
    normalized.insert(QStringLiteral("liveAccountId"), liveAccountId);
    normalized.insert(QStringLiteral("simAccountId"), simAccountId);
    const QString configuredAccountRuntimeStrategyId = normalizeStrategyIdAlias(
        normalized.value(QStringLiteral("accountRuntimeStrategyId")).toString());
    const QString configuredGmStrategyId = normalizeStrategyIdAlias(
        normalized.value(QStringLiteral("gmStrategyId")).toString());
    const QString legacyStrategyId = normalized.value(QStringLiteral("strategyId")).toString().trimmed();
    QString normalizedGmStrategyId = normalizeStrategyIdAlias(
        normalized.value(QStringLiteral("runtimeStrategyId")).toString());

    normalized.insert(QStringLiteral("accountId"), accountId);
    normalized.insert(QStringLiteral("boundStrategyId"), boundStrategyId);
    normalized.insert(QStringLiteral("boundStrategyName"), boundStrategyName);
    normalized.insert(QStringLiteral("boundStrategies"), normalizedBoundStrategies);
    normalized.insert(QStringLiteral("boundStrategyIds"), normalizedBoundStrategyIds);

    QString accountRuntimeStrategyId = configuredAccountRuntimeStrategyId;
    if (accountRuntimeStrategyId.isEmpty() && !accountId.isEmpty()) {
        accountRuntimeStrategyId = buildAccountRuntimeStrategyIdAlias(accountId);
    }
    normalized.insert(QStringLiteral("accountRuntimeStrategyId"), accountRuntimeStrategyId);

    if (!configuredGmStrategyId.isEmpty()) {
        normalizedGmStrategyId = configuredGmStrategyId;
    }

    if (normalizedGmStrategyId.isEmpty() && !accountId.isEmpty() && !boundStrategyId.isEmpty()) {
        normalizedGmStrategyId = buildRuntimeStrategyIdAlias(accountId, boundStrategyId);
    }

    if (normalizedGmStrategyId.isEmpty() && !legacyStrategyId.isEmpty()) {
        normalizedGmStrategyId = normalizeStrategyIdAlias(legacyStrategyId);
    }

    normalized.insert(QStringLiteral("gmStrategyId"), normalizedGmStrategyId);
    normalized.insert(QStringLiteral("runtimeStrategyId"), normalizedGmStrategyId);
    normalized.insert(QStringLiteral("strategyId"), normalizedGmStrategyId);
    normalized.insert(QStringLiteral("mode"), QStringLiteral("1"));
    normalized.insert(QStringLiteral("serverUrl"), normalized.value(QStringLiteral("serverUrl")).toString().trimmed());

    normalized.insert(QStringLiteral("symbols"), QString());

    QStringList processNames;
    const QVariant processValue = normalized.value(QStringLiteral("clientProcessNames"));
    if (processValue.canConvert<QStringList>()) {
        processNames = processValue.toStringList();
    } else {
        const QStringList rawNames = processValue.toString().split(',', Qt::SkipEmptyParts);
        for (const QString& rawName : rawNames) {
            const QString trimmedName = rawName.trimmed();
            if (!trimmedName.isEmpty()) {
                processNames.append(trimmedName);
            }
        }
    }

    if (processNames.isEmpty()) {
        processNames = defaultClientProcessNames();
    }
    normalized.insert(QStringLiteral("clientProcessNames"), processNames);

    return normalized;
}

bool TradingConnectionConfigService::detectClientProcessLocked(QString* matchedProcessName) const
{
    const QStringList processNames = m_currentConfiguration.value(QStringLiteral("clientProcessNames")).toStringList();
    if (processNames.isEmpty()) {
        if (matchedProcessName) {
            matchedProcessName->clear();
        }
        return false;
    }

    QProcess process;
    process.start(QStringLiteral("tasklist"), {QStringLiteral("/FO"), QStringLiteral("CSV"), QStringLiteral("/NH")});
    if (!process.waitForFinished(3000) || process.exitStatus() != QProcess::NormalExit) {
        if (matchedProcessName) {
            matchedProcessName->clear();
        }
        return false;
    }

    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput()).toLower();
    for (const QString& processName : processNames) {
        const QString candidate = processName.trimmed().toLower();
        if (!candidate.isEmpty() && output.contains(QStringLiteral("\"") + candidate + QStringLiteral("\""))) {
            if (matchedProcessName) {
                *matchedProcessName = processName.trimmed();
            }
            return true;
        }
    }

    if (matchedProcessName) {
        matchedProcessName->clear();
    }
    return false;
}