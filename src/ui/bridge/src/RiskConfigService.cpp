#include "RiskConfigService.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

QJsonObject toJsonObject(const QVariantMap& map)
{
    return QJsonObject::fromVariantMap(map);
}

QVariantMap toVariantMap(const QJsonObject& object)
{
    return object.toVariantMap();
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

QVariantMap normalizeRiskConfiguration(const QVariantMap& rawConfiguration)
{
    QVariantMap normalized = rawConfiguration;

    const auto applyAliasGroup = [&normalized](const QStringList& keys) {
        const QVariant resolved = firstConfiguredValue(normalized, keys);
        if (!resolved.isValid()) {
            return;
        }

        for (const QString& key : keys) {
            normalized.insert(key, resolved);
        }
    };

    applyAliasGroup({QStringLiteral("maxTotalExposure"), QStringLiteral("maxPositionRatio")});
    applyAliasGroup({QStringLiteral("maxPositionPercent"), QStringLiteral("maxSinglePositionRatio"), QStringLiteral("positionPercent"), QStringLiteral("position_size"), QStringLiteral("positionSize")});
    applyAliasGroup({QStringLiteral("maxDrawdownLimit"), QStringLiteral("max_drawdown_limit")});
    applyAliasGroup({QStringLiteral("stopLossPercent"), QStringLiteral("stop_loss"), QStringLiteral("stopLoss")});
    applyAliasGroup({QStringLiteral("takeProfitPercent"), QStringLiteral("take_profit"), QStringLiteral("takeProfit")});
    applyAliasGroup({QStringLiteral("rebalanceDays"), QStringLiteral("rebalance_days"), QStringLiteral("rebalancingPeriod"), QStringLiteral("rebalanceFrequency")});
    applyAliasGroup({QStringLiteral("commissionRate"), QStringLiteral("commission"), QStringLiteral("transactionCost")});
    applyAliasGroup({QStringLiteral("slippageRate"), QStringLiteral("slippage"), QStringLiteral("slippageCost"), QStringLiteral("slippageLimit")});

    return normalized;
}

} // namespace

RiskConfigService* RiskConfigService::m_instance = nullptr;
QMutex RiskConfigService::m_instanceMutex;

RiskConfigService* RiskConfigService::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new RiskConfigService();
        m_instance->initialize();
    }
    return m_instance;
}

RiskConfigService::RiskConfigService(QObject* parent)
    : QObject(parent)
    , m_initialized(false)
{
}

void RiskConfigService::initialize()
{
    QMutexLocker locker(&m_mutex);
    if (m_initialized) {
        return;
    }

    loadPersistedState();
    m_initialized = true;
    emit initializedChanged();
}

QVariantMap RiskConfigService::loadCurrentConfiguration()
{
    QMutexLocker locker(&m_mutex);
    if (!m_initialized) {
        loadPersistedState();
        m_initialized = true;
        emit initializedChanged();
    }
    return normalizeRiskConfiguration(m_currentConfiguration);
}

QVariantMap RiskConfigService::loadAppliedConfiguration()
{
    QMutexLocker locker(&m_mutex);
    if (!m_initialized) {
        loadPersistedState();
        m_initialized = true;
        emit initializedChanged();
    }
    return normalizeRiskConfiguration(m_appliedConfiguration);
}

bool RiskConfigService::saveConfiguration(const QVariantMap& configuration)
{
    QVariantMap savedConfiguration;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_initialized) {
            loadPersistedState();
            m_initialized = true;
            emit initializedChanged();
        }

        m_currentConfiguration = normalizeRiskConfiguration(configuration);
        if (!persistState()) {
            emit errorOccurred(QStringLiteral("风险配置保存失败"));
            return false;
        }
        savedConfiguration = m_currentConfiguration;
    }

    emit currentConfigurationChanged();
    emit configurationSaved(savedConfiguration);
    return true;
}

bool RiskConfigService::applyConfiguration(const QVariantMap& configuration)
{
    QVariantMap appliedConfiguration;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_initialized) {
            loadPersistedState();
            m_initialized = true;
            emit initializedChanged();
        }

        m_currentConfiguration = normalizeRiskConfiguration(configuration);
        m_appliedConfiguration = m_currentConfiguration;
        if (!persistState()) {
            emit errorOccurred(QStringLiteral("风险配置应用失败"));
            return false;
        }
        appliedConfiguration = m_appliedConfiguration;
    }

    emit currentConfigurationChanged();
    emit appliedConfigurationChanged();
    emit configurationSaved(appliedConfiguration);
    emit configurationApplied(appliedConfiguration);
    return true;
}

bool RiskConfigService::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}

QVariantMap RiskConfigService::currentConfiguration() const
{
    QMutexLocker locker(&m_mutex);
    return normalizeRiskConfiguration(m_currentConfiguration);
}

QVariantMap RiskConfigService::appliedConfiguration() const
{
    QMutexLocker locker(&m_mutex);
    return normalizeRiskConfiguration(m_appliedConfiguration);
}

QString RiskConfigService::configFilePath() const
{
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(baseDir).filePath(QStringLiteral("risk/risk_configuration.json"));
}

void RiskConfigService::loadPersistedState()
{
    m_currentConfiguration.clear();
    m_appliedConfiguration.clear();

    QFile file(configFilePath());
    if (!file.exists()) {
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(QStringLiteral("无法读取风险配置文件"));
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject()) {
        emit errorOccurred(QStringLiteral("风险配置文件格式无效"));
        return;
    }

    const QJsonObject root = document.object();
    m_currentConfiguration = normalizeRiskConfiguration(toVariantMap(root.value(QStringLiteral("currentConfiguration")).toObject()));
    m_appliedConfiguration = normalizeRiskConfiguration(toVariantMap(root.value(QStringLiteral("appliedConfiguration")).toObject()));
}

bool RiskConfigService::persistState()
{
    const QString path = configFilePath();
    QFileInfo fileInfo(path);
    QDir dir = fileInfo.dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        emit errorOccurred(QStringLiteral("无法创建风险配置目录"));
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("currentConfiguration"), toJsonObject(m_currentConfiguration));
    root.insert(QStringLiteral("appliedConfiguration"), toJsonObject(m_appliedConfiguration));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred(QStringLiteral("无法写入风险配置文件"));
        return false;
    }

    const QJsonDocument document(root);
    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        emit errorOccurred(QStringLiteral("风险配置文件提交失败"));
        return false;
    }

    return true;
}