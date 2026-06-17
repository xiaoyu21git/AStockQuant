#include "TradingConnectionConfigService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutexLocker>
#include <QTimer>
#include <QDebug>

namespace bridge {

TradingConnectionConfigService::TradingConnectionConfigService(QObject* parent)
    : QObject(parent) {
    m_configFilePath = resolveConfigFilePath();
}

QVariantMap TradingConnectionConfigService::currentConfiguration() const {
    QMutexLocker lock(&m_mutex);
    return m_currentConfig;
}

QString TradingConnectionConfigService::configFilePath() const {
    return m_configFilePath;
}

// ═══════════════════════════════════════════════════════════════════
// 配置持久化
// ═══════════════════════════════════════════════════════════════════

QVariantMap TradingConnectionConfigService::loadConfiguration() {
    QMutexLocker lock(&m_mutex);
    m_currentConfig = readConfigFile();
    if (m_currentConfig.isEmpty()) {
        m_currentConfig = defaultConfiguration();
    }
    m_initialized = true;
    emit currentConfigurationChanged();
    return m_currentConfig;
}

bool TradingConnectionConfigService::saveConfiguration(const QVariantMap& payload) {
    QMutexLocker lock(&m_mutex);
    QVariantMap normalized = normalizeConfiguration(payload);
    m_currentConfig = normalized;
    if (writeConfigFile(normalized)) {
        emit currentConfigurationChanged();
        return true;
    }
    emit errorOccurred(QStringLiteral("保存交易配置失败"));
    return false;
}

QVariantMap TradingConnectionConfigService::defaultConfiguration() const {
    QVariantMap cfg;
    cfg["enabled"] = false;
    cfg["readOnly"] = false;
    cfg["boundStrategyId"] = QString();
    cfg["boundStrategyName"] = QString();
    cfg["runtimeStrategyId"] = QString();
    cfg["accountId"] = QString();
    cfg["liveAccountId"] = QString();
    cfg["simAccountId"] = QString();
    cfg["gmStrategyId"] = QString();
    cfg["token"] = QString();
    cfg["serverUrl"] = QString();
    cfg["accountProfile"] = QStringLiteral("simulation");
    cfg["boundStrategies"] = QVariantList();
    cfg["runtimeRuleDefaults"] = QVariantMap();
    cfg["symbols"] = QVariantList();
    cfg["updatedAt"] = QString();
    cfg["version"] = 1;
    return cfg;
}

// ═══════════════════════════════════════════════════════════════════
// 策略绑定管理
// ═══════════════════════════════════════════════════════════════════

QVariantMap TradingConnectionConfigService::bindStrategyConfiguration(
    const QString& strategyId, const QString& strategyName,
    bool isPrimary, bool autoStart) {

    QMutexLocker lock(&m_mutex);
    if (!m_initialized) {
        m_currentConfig = readConfigFile();
        if (m_currentConfig.isEmpty()) m_currentConfig = defaultConfiguration();
        m_initialized = true;
    }

    QString sid = sanitizeStrategyId(strategyId);

    QVariantMap entry;
    entry["strategyId"] = sid;
    entry["strategyName"] = strategyName;
    entry["isPrimary"] = isPrimary;
    entry["autoStart"] = autoStart;
    entry["enabled"] = true;
    entry["readOnly"] = false;

    if (isPrimary) {
        m_currentConfig["boundStrategyId"] = sid;
        m_currentConfig["boundStrategyName"] = strategyName;
        m_currentConfig["runtimeStrategyId"] = sid;
    }

    mergeBoundStrategy(entry);
    writeConfigFile(m_currentConfig);

    QVariantMap result;
    result["success"] = true;
    result["message"] = QStringLiteral("策略绑定成功");
    result["enabled"] = true;
    result["readOnly"] = false;
    result["readyForTrading"] = true;

    QVariantMap startupGate = evaluateStartupGate(false);
    result["startupGate"] = startupGate;

    emit currentConfigurationChanged();
    return result;
}

QVariantMap TradingConnectionConfigService::addBoundStrategyConfiguration(
    const QString& strategyId, const QString& strategyName,
    bool isPrimary, bool autoStart) {
    return bindStrategyConfiguration(strategyId, strategyName, isPrimary, autoStart);
}

QVariantMap TradingConnectionConfigService::removeBoundStrategyConfiguration(
    const QString& strategyId) {

    QMutexLocker lock(&m_mutex);
    if (!m_initialized) {
        m_currentConfig = readConfigFile();
        m_initialized = true;
    }

    QString sid = sanitizeStrategyId(strategyId);
    bool removed = removeBoundStrategy(sid);

    // 如果移除的是当前主策略，清除相关引用
    if (m_currentConfig.value("boundStrategyId").toString() == sid) {
        m_currentConfig["boundStrategyId"] = QString();
        m_currentConfig["boundStrategyName"] = QString();
    }
    if (m_currentConfig.value("runtimeStrategyId").toString() == sid) {
        m_currentConfig["runtimeStrategyId"] = QString();
    }

    writeConfigFile(m_currentConfig);
    emit currentConfigurationChanged();

    QVariantMap result;
    result["success"] = removed;
    result["message"] = removed ? QStringLiteral("策略解绑成功")
                                 : QStringLiteral("未找到要解绑的策略");
    return result;
}

// ═══════════════════════════════════════════════════════════════════
// 启动门控
// ═══════════════════════════════════════════════════════════════════

QVariantMap TradingConnectionConfigService::evaluateStartupGate(bool forceRecheck) {
    Q_UNUSED(forceRecheck)
    QMutexLocker lock(&m_mutex);

    if (!m_initialized) {
        m_currentConfig = readConfigFile();
        m_initialized = true;
    }

    QStringList reasons;
    bool ready = true;

    // 检查是否有绑定策略
    QVariantList strategies = boundStrategies();
    if (strategies.isEmpty()) {
        ready = false;
        reasons << QStringLiteral("未绑定任何策略");
    }

    // 检查是否有 accountId 配置
    QString accountId = m_currentConfig.value("accountId").toString();
    if (accountId.isEmpty()) {
        QString simId = m_currentConfig.value("simAccountId").toString();
        QString liveId = m_currentConfig.value("liveAccountId").toString();
        if (simId.isEmpty() && liveId.isEmpty()) {
            ready = false;
            reasons << QStringLiteral("未配置交易账户");
        }
    }

    return buildStartupGateResult(ready,
        ready ? QStringLiteral("OK") : QStringLiteral("CONFIG_INCOMPLETE"),
        reasons);
}

// ═══════════════════════════════════════════════════════════════════
// 客户端进程状态
// ═══════════════════════════════════════════════════════════════════

QVariantMap TradingConnectionConfigService::refreshClientProcessStatus() {
    return detectClientProcesses();
}

void TradingConnectionConfigService::refreshClientProcessStatusAsync() {
    QTimer::singleShot(0, this, [this]() {
        QVariantMap status = detectClientProcesses();
        emit clientProcessStatusChanged(status);
    });
}

// ═══════════════════════════════════════════════════════════════════
// 内部辅助方法
// ═══════════════════════════════════════════════════════════════════

QString TradingConnectionConfigService::resolveConfigFilePath() const {
    const QString baseDir = QCoreApplication::applicationDirPath();
    return QDir(baseDir).filePath(QStringLiteral("config/trading_connection.json"));
}

QVariantMap TradingConnectionConfigService::normalizeConfiguration(const QVariantMap& raw) const {
    QVariantMap normalized;
    QVariantMap defaults = defaultConfiguration();

    // 键名规范化：统一使用 camelCase
    auto normalizeKey = [](const QString& key) -> QString {
        if (key == "accountId" || key == "liveAccountId" || key == "simAccountId"
            || key == "gmStrategyId" || key == "strategy_id" || key == "gm_strategy_id")
            return key;
        // 将下划线键转为驼峰
        if (key.contains('_')) {
            QString result;
            bool capitalize = false;
            for (QChar c : key) {
                if (c == '_') { capitalize = true; continue; }
                result += capitalize ? c.toUpper() : c;
                capitalize = false;
            }
            return result;
        }
        return key;
    };

    for (auto it = raw.begin(); it != raw.end(); ++it) {
        normalized[normalizeKey(it.key())] = it.value();
    }

    // 确保默认键存在
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        if (!normalized.contains(it.key())) {
            normalized[it.key()] = it.value();
        }
    }

    // accountId 别名合并
    if (normalized.value("accountId").toString().isEmpty()) {
        QString simId = normalized.value("simAccountId").toString();
        if (!simId.isEmpty()) {
            normalized["accountId"] = simId;
        } else {
            normalized["accountId"] = normalized.value("liveAccountId").toString();
        }
    }

    normalized["updatedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    normalized["version"] = 1;
    return normalized;
}

bool TradingConnectionConfigService::writeConfigFile(const QVariantMap& config) const {
    const QString path = m_configFilePath;
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonDocument doc(QJsonObject::fromVariantMap(config));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[TradingConnectionConfigService] Cannot write to" << path;
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QVariantMap TradingConnectionConfigService::readConfigFile() const {
    QFile file(m_configFilePath);
    if (!file.exists()) return QVariantMap();
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QVariantMap();

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) return QVariantMap();

    return normalizeConfiguration(doc.object().toVariantMap());
}

QVariantMap TradingConnectionConfigService::buildStartupGateResult(
    bool ready, const QString& reasonCode, const QStringList& reasons) const {
    QVariantMap gate;
    gate["ready"] = ready;
    gate["reasonCode"] = reasonCode;
    gate["reasons"] = reasons;
    return gate;
}

QVariantMap TradingConnectionConfigService::detectClientProcesses() const {
    QVariantMap status;
    status["running"] = false;
    status["processNames"] = QVariantList();
    status["clientProcessRunning"] = false;
    status["clientProcessStatus"] = QStringLiteral("未检测");
    // MVP: 不做实际的进程检测（原实现依赖 tasklist 命令和特定进程名）
    return status;
}

QString TradingConnectionConfigService::sanitizeStrategyId(const QString& raw) const {
    QString sanitized = raw.trimmed().toLower();
    // 只保留字母数字和下划线
    QString result;
    for (QChar c : sanitized) {
        if (c.isLetterOrNumber() || c == '_' || c == '-') {
            result += c;
        }
    }
    // 限制长度
    if (result.length() > 64) result = result.left(64);
    return result.isEmpty() ? raw : result;
}

QVariantList TradingConnectionConfigService::boundStrategies() const {
    return m_currentConfig.value("boundStrategies").toList();
}

void TradingConnectionConfigService::setBoundStrategies(const QVariantList& strategies) {
    m_currentConfig["boundStrategies"] = strategies;
}

void TradingConnectionConfigService::mergeBoundStrategy(const QVariantMap& entry) {
    QVariantList strategies = boundStrategies();
    QString sid = entry.value("strategyId").toString();

    // 更新已有条目或追加
    for (int i = 0; i < strategies.size(); ++i) {
        QVariantMap item = strategies[i].toMap();
        if (item.value("strategyId").toString() == sid) {
            strategies[i] = entry;
            setBoundStrategies(strategies);
            return;
        }
    }
    strategies.append(entry);
    setBoundStrategies(strategies);
}

bool TradingConnectionConfigService::removeBoundStrategy(const QString& strategyId) {
    QVariantList strategies = boundStrategies();
    for (int i = 0; i < strategies.size(); ++i) {
        if (strategies[i].toMap().value("strategyId").toString() == strategyId) {
            strategies.removeAt(i);
            setBoundStrategies(strategies);
            return true;
        }
    }
    return false;
}

TradingConnectionConfigService* TradingConnectionConfigService::instance() {
    static TradingConnectionConfigService s_instance;
    return &s_instance;
}

} // namespace bridge
