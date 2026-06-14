// RiskControlBridge.cpp — 纯桥接层
// 业务逻辑已下沉到 src/domain/risk/LiveRiskEvaluator

#include "../include/RiskControlBridge.h"
#include "PositionAccountService.h"
#include "RiskConfigService.h"

#include "LiveRiskEvaluator.h"

#include <QMutexLocker>

RiskControlBridge* RiskControlBridge::s_instance = nullptr;
QMutex RiskControlBridge::s_mutex;

RiskControlBridge* RiskControlBridge::instance() {
    QMutexLocker locker(&s_mutex);
    if (!s_instance) s_instance = new RiskControlBridge();
    return s_instance;
}

RiskControlBridge::RiskControlBridge(QObject* parent) : QObject(parent) {}

void RiskControlBridge::initialize() {
    {
        QMutexLocker locker(&m_mutex);
        if (m_initialized) return;
        m_initialized = true;
    }
    auto* posSvc = PositionAccountService::instance();
    if (posSvc) {
        posSvc->initialize();
        QObject::connect(posSvc, &PositionAccountService::accountSnapshotChanged,
            this, [this]() { refreshMetrics(); });
    }
    auto* riskCfgSvc = RiskConfigService::instance();
    if (riskCfgSvc) {
        QObject::connect(riskCfgSvc, &RiskConfigService::appliedConfigurationChanged,
            this, [this]() { refreshMetrics(); });
    }
    refreshMetrics();
    emit initializedChanged();
}

bool RiskControlBridge::isInitialized() const { QMutexLocker locker(&m_mutex); return m_initialized; }
double RiskControlBridge::currentDrawdownPercent() const { QMutexLocker locker(&m_mutex); return m_currentDrawdownPercent; }
double RiskControlBridge::varUsagePercent() const { QMutexLocker locker(&m_mutex); return m_varUsagePercent; }
double RiskControlBridge::currentTotalExposurePercent() const { QMutexLocker locker(&m_mutex); return m_currentTotalExposurePercent; }

void RiskControlBridge::refreshMetrics() {
    auto* posSvc = PositionAccountService::instance();
    if (!posSvc) return;
    posSvc->initialize();

    const QVariantMap snapshot = posSvc->accountSnapshot();
    domain::risk::AccountSnapshot account;
    account.totalAsset = snapshot.value("totalAsset").toDouble();
    account.marketValue = snapshot.value("marketValue").toDouble();
    if (account.totalAsset <= 0.0) return;

    auto* riskCfgSvc = RiskConfigService::instance();
    domain::risk::RiskConfig config;
    if (riskCfgSvc) {
        QVariantMap cfg = riskCfgSvc->appliedConfiguration();
        config.maxTotalExposureRatio = cfg.value("maxTotalExposureRatio", 0.67).toDouble();
        config.maxSinglePositionRatio = cfg.value("maxSinglePositionRatio", 0.20).toDouble();
        config.stopLossRatio = cfg.value("stopLossRatio", 0.10).toDouble();
    }

    domain::risk::LiveRiskEvaluator evaluator;
    auto metrics = evaluator.computeMetrics(account, config, m_peakObservedTotalAsset);

    {
        QMutexLocker locker(&m_mutex);
        m_currentDrawdownPercent = metrics.currentDrawdownPercent;
        m_varUsagePercent = metrics.varUsagePercent;
        m_currentTotalExposurePercent = metrics.currentTotalExposurePercent;
    }
    emit metricsChanged();
}