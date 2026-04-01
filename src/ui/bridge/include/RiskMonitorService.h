#pragma once

#include <QObject>
#include <QMutex>
#include <QVariantMap>

class RiskMonitorService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)

public:
    static RiskMonitorService* instance();

    RiskMonitorService(const RiskMonitorService&) = delete;
    RiskMonitorService& operator=(const RiskMonitorService&) = delete;

    Q_INVOKABLE void initialize();
    Q_INVOKABLE QVariantMap buildPortfolioSnapshot(const QVariantMap& strategy,
                                                  const QVariantMap& latestBacktest = QVariantMap());

    bool isInitialized() const;

signals:
    void initializedChanged();

private:
    explicit RiskMonitorService(QObject* parent = nullptr);

    static RiskMonitorService* m_instance;
    static QMutex m_instanceMutex;

    mutable QMutex m_mutex;
    bool m_initialized;
};