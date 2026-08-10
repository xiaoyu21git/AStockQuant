#include "RiskConfigService.h"
#include "ConfigVariantAdapter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include "foundation/config/ConfigManager.hpp"
#include "foundation/log/logging.hpp"

namespace bridge {

RiskConfigService::RiskConfigService(QObject* parent)
    : QObject(parent) {}

QVariantMap RiskConfigService::appliedConfiguration() const {
    return m_appliedConfig;
}

void RiskConfigService::initialize() {
    if (m_initialized) return;
    loadAppliedConfiguration();
    loadCurrentConfiguration();
    m_initialized = true;
}

QVariantMap RiskConfigService::loadCurrentConfiguration() {
    auto cfg = foundation::config::ConfigManager::instance()
        .loadConfigFile(foundation::config::ConfigFile::RiskConfig);
    if (!cfg || cfg->isNull()) {
        m_currentConfig = defaultConfiguration();
        return m_currentConfig;
    }
    auto current = cfg->getPath("currentConfiguration", '.');
    if (current.isNull()) {
        m_currentConfig = defaultConfiguration();
        return m_currentConfig;
    }
    m_currentConfig = toVariantMap(current);
    return m_currentConfig;
}

QVariantMap RiskConfigService::loadAppliedConfiguration() {
    if (!m_appliedConfig.isEmpty()) return m_appliedConfig;
    auto cfg = foundation::config::ConfigManager::instance()
        .loadConfigFile(foundation::config::ConfigFile::RiskConfig);
    if (!cfg || cfg->isNull()) {
        m_appliedConfig = defaultConfiguration();
        return m_appliedConfig;
    }
    auto applied = cfg->getPath("appliedConfiguration", '.');
    if (applied.isNull()) {
        m_appliedConfig = defaultConfiguration();
        return m_appliedConfig;
    }
    m_appliedConfig = toVariantMap(applied);
    return m_appliedConfig;
}

bool RiskConfigService::saveConfiguration(const QVariantMap& config) {
    QVariantMap normalized = normalizeConfiguration(config);
    m_currentConfig = normalized;
    if (writeConfigFile()) {
        emit configurationSaved();
        return true;
    }
    emit errorOccurred(QStringLiteral("保存风控配置失败"));
    return false;
}

bool RiskConfigService::applyConfiguration(const QVariantMap& config) {
    QVariantMap normalized = normalizeConfiguration(config);
    m_currentConfig = normalized;
    m_appliedConfig = normalized;
    writeConfigFile();
    emit appliedConfigurationChanged();
    emit configurationApplied();
    return true;
}

QString RiskConfigService::configFilePath() const {
    return QString::fromStdString(
        foundation::config::ConfigManager::instance()
            .configFilePath(foundation::config::ConfigFile::RiskConfig));
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

    // 费率
    cfg["commissionRate"] = 0.0003;
    cfg["minCommission"] = 5.0;
    cfg["stampTaxRate"] = 0.001;

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

bool RiskConfigService::writeConfigFile() const {
    QVariantMap wrapper;
    wrapper["appliedConfiguration"] = m_appliedConfig;
    wrapper["currentConfiguration"] = m_currentConfig;
    auto node = toConfigNode(wrapper);
    return foundation::config::ConfigManager::instance()
        .saveConfigFile(foundation::config::ConfigFile::RiskConfig, node);
}

} // namespace bridge
