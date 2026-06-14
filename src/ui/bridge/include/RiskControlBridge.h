#pragma once

#include <QObject>
#include <QMutex>
#include <QVariantMap>

namespace domain::strategy {
struct RiskLiveMetrics;
}

class RiskControlBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(double currentDrawdownPercent READ currentDrawdownPercent NOTIFY metricsChanged)
    Q_PROPERTY(double varUsagePercent READ varUsagePercent NOTIFY metricsChanged)
    Q_PROPERTY(double currentTotalExposurePercent READ currentTotalExposurePercent NOTIFY metricsChanged)

public:
    static RiskControlBridge* instance();

    RiskControlBridge(const RiskControlBridge&) = delete;
    RiskControlBridge& operator=(const RiskControlBridge&) = delete;

    Q_INVOKABLE void initialize();

    bool isInitialized() const;
    double currentDrawdownPercent() const;
    double varUsagePercent() const;
    double currentTotalExposurePercent() const;

signals:
    void initializedChanged();
    void metricsChanged();

private:
    explicit RiskControlBridge(QObject* parent = nullptr);
    void refreshMetrics();

    static RiskControlBridge* s_instance;
    static QMutex s_mutex;

    mutable QMutex m_mutex;
    bool m_initialized{false};
    double m_currentDrawdownPercent{0.0};
    double m_varUsagePercent{0.0};
    double m_currentTotalExposurePercent{0.0};
    double m_peakObservedTotalAsset{0.0};
};