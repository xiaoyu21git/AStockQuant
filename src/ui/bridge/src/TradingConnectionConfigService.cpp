#include "TradingConnectionConfigService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QProcess>
#include <QSaveFile>

namespace {

QVariantMap baseTradingConfiguration()
{
    QVariantMap config;
    config.insert(QStringLiteral("provider"), QStringLiteral("jujin"));
    config.insert(QStringLiteral("enabled"), false);
    config.insert(QStringLiteral("simtradeOnly"), false);
    config.insert(QStringLiteral("readOnly"), true);
    config.insert(QStringLiteral("token"), QString());
    config.insert(QStringLiteral("accountId"), QString());
    config.insert(QStringLiteral("strategyId"), QStringLiteral("astock_quant_ui"));
    config.insert(QStringLiteral("mode"), QStringLiteral("1"));
    config.insert(QStringLiteral("serverUrl"), QString());
    config.insert(QStringLiteral("symbols"), QStringLiteral("600000.SH,000001.SZ,600519.SH,300750.SZ"));
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
    {
        QMutexLocker locker(&m_mutex);
        if (m_initialized) {
            return;
        }

        loadPersistedState();
        m_initialized = true;
    }

    refreshClientProcessStatus();
    emit initializedChanged();
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

        m_currentConfiguration = normalizedConfiguration(configuration);
        m_currentConfiguration.insert(
            QStringLiteral("updatedAt"),
            QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

        if (!persistState()) {
            emit errorOccurred(QStringLiteral("交易连接配置保存失败"));
            return false;
        }

        savedConfiguration = m_currentConfiguration;
    }

    refreshClientProcessStatus();
    emit currentConfigurationChanged();
    emit configurationSaved(savedConfiguration);
    return true;
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
    normalized.insert(QStringLiteral("simtradeOnly"), normalized.value(QStringLiteral("simtradeOnly"), false).toBool());
    normalized.insert(QStringLiteral("readOnly"), normalized.value(QStringLiteral("readOnly"), true).toBool());
    normalized.insert(QStringLiteral("token"), normalized.value(QStringLiteral("token")).toString().trimmed());
    normalized.insert(QStringLiteral("accountId"), normalized.value(QStringLiteral("accountId")).toString().trimmed());
    normalized.insert(QStringLiteral("strategyId"), normalized.value(QStringLiteral("strategyId")).toString().trimmed());
    normalized.insert(
        QStringLiteral("mode"),
        normalized.value(QStringLiteral("mode")).toString().trimmed().isEmpty()
            ? QStringLiteral("1")
            : normalized.value(QStringLiteral("mode")).toString().trimmed());
    normalized.insert(QStringLiteral("serverUrl"), normalized.value(QStringLiteral("serverUrl")).toString().trimmed());
    normalized.insert(QStringLiteral("symbols"), normalized.value(QStringLiteral("symbols")).toString().trimmed());

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