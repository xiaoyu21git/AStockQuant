#pragma once

#include <QObject>
#include <QMutex>
#include <QVariantMap>

class QTimer;

class TradingMarketCalendarService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(QVariantMap currentSessionSnapshot READ currentSessionSnapshot NOTIFY currentSessionSnapshotChanged)
    Q_PROPERTY(bool holidayAware READ isHolidayAware NOTIFY currentSessionSnapshotChanged)

public:
    static TradingMarketCalendarService* instance();

    TradingMarketCalendarService(const TradingMarketCalendarService&) = delete;
    TradingMarketCalendarService& operator=(const TradingMarketCalendarService&) = delete;

    Q_INVOKABLE void initialize();
    Q_INVOKABLE bool isInitialized() const;
    Q_INVOKABLE QVariantMap currentSessionSnapshot() const;
    Q_INVOKABLE bool isHolidayAware() const;
    Q_INVOKABLE bool isTradingSessionOpen() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setSessionSnapshotOverrideForTesting(const QVariantMap& snapshot);
    Q_INVOKABLE void clearSessionSnapshotOverrideForTesting();

signals:
    void initializedChanged();
    void currentSessionSnapshotChanged();

private:
    explicit TradingMarketCalendarService(QObject* parent = nullptr);

    static TradingMarketCalendarService* m_instance;
    static QMutex m_instanceMutex;

    mutable QMutex m_mutex;
    bool m_initialized;
    QVariantMap m_calendarBase;
    QVariantMap m_currentSessionSnapshot;
    QVariantMap m_testingSessionSnapshotOverride;
    bool m_hasTestingSessionSnapshotOverride;
    QTimer* m_refreshTimer;
};