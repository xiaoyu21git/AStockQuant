#include "RiskConfigService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QDebug>

namespace bridge {

RiskConfigService::RiskConfigService(QObject* parent)
    : QObject(parent) {}

QVariantMap RiskConfigService::appliedConfiguration() const {
    return m_appliedConfig;
}

void RiskConfigService::initialize() {
    if (m_initialized) return;
    m_appliedConfig = loadAppliedConfiguration();
    m_initialized = true;
}

QVariantMap RiskConfigService::loadCurrentConfiguration() {
    return readConfigFile();
}

QVariantMap RiskConfigService::loadAppliedConfiguration() {
    if (!m_appliedConfig.isEmpty()) return m_appliedConfig;
    QVariantMap config = readConfigFile();
    if (!config.isEmpty()) {
        m_appliedConfig = config;
        return m_appliedConfig;
    }
    m_appliedConfig = defaultConfiguration();
    return m_appliedConfig;
}

bool RiskConfigService::saveConfiguration(const QVariantMap& config) {
    QVariantMap normalized = normalizeConfiguration(config);
    if (writeConfigFile(normalized)) {
        m_appliedConfig = normalized;
        emit configurationSaved();
        return true;
    }
    emit errorOccurred(QStringLiteral("保存风控配置失败"));
    return false;
}

bool RiskConfigService::applyConfiguration(const QVariantMap& config) {
    QVariantMap normalized = normalizeConfiguration(config);
    m_appliedConfig = normalized;
    writeConfigFile(normalized);
    emit appliedConfigurationChanged();
    emit configurationApplied();
    return true;
}

QString RiskConfigService::configFilePath() const {
    const QString baseDir = QCoreApplication::applicationDirPath();
    return QDir(baseDir).filePath(QStringLiteral("config/risk_config.json"));
}

QVariantMap RiskConfigService::defaultConfiguration() const {
    QVariantMap cfg;

    // 止损止盈
    cfg["stopLossPercent"] = 10.0;
    cfg["takeProfitPercent"] = 20.0;

    // 回撤限制
    cfg["maxDrawdownLimitPercent"] = 12.0;
    cfg["maxDailyLossPercent"] = 5.0;

    // 持仓限制
    cfg["maxPositionPercent"] = 15.0;
    cfg["maxTotalExposurePercent"] = 67.0;

    // 熔断阈值
    cfg["breakerLevel1Percent"] = 5.0;
    cfg["breakerLevel2Percent"] = 8.0;
    cfg["breakerLevel3Percent"] = 12.0;

    // 订单限制
    cfg["orderSizeLimitWan"] = 500.0;
    cfg["slippageLimitPercent"] = 2.0;
    cfg["turnoverLimitWan"] = 5000.0;

    // VaR 参数
    cfg["varConfidenceLevel"] = 0.95;
    cfg["varLookbackDays"] = 60;

    cfg["version"] = 1;
    return cfg;
}

QVariantMap RiskConfigService::normalizeConfiguration(const QVariantMap& raw) const {
    QVariantMap normalized;
    QVariantMap defaults = defaultConfiguration();

    // 复制已知键，保留未知键
    for (auto it = raw.begin(); it != raw.end(); ++it) {
        normalized[it.key()] = it.value();
    }

    // 确保默认值存在
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        if (!normalized.contains(it.key())) {
            normalized[it.key()] = it.value();
        }
    }

    normalized["version"] = 1;
    return normalized;
}

bool RiskConfigService::writeConfigFile(const QVariantMap& config) const {
    const QString path = configFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonDocument doc(QJsonObject::fromVariantMap(config));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[RiskConfigService] Cannot write config to" << path;
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QVariantMap RiskConfigService::readConfigFile() const {
    const QString path = configFilePath();
    QFile file(path);
    if (!file.exists()) {
        return defaultConfiguration();
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[RiskConfigService] Cannot read config from" << path;
        return QVariantMap();
    }
    QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "[RiskConfigService] JSON parse error:" << err.errorString();
        return QVariantMap();
    }
    return doc.object().toVariantMap();
}

} // namespace bridge
