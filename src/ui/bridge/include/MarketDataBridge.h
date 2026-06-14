#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>

// 行情桥接层（预留接口，历史数据查询已改用 HistoricalView，实时行情待后续接入）

class MarketDataBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList bars READ bars NOTIFY barsChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)

public:
    explicit MarketDataBridge(QObject* parent = nullptr);

    Q_INVOKABLE void loadBars(const QStringList& symbols,
                               const QString& startDate,
                               const QString& endDate);

    Q_INVOKABLE QVariantMap getCrossSection(const QString& field,
                                              const QString& date,
                                              const QStringList& symbols = {});

    Q_INVOKABLE QVariantList getIndexConstituents(const QString& indexSymbol,
                                                    const QString& snapshotDate);

    Q_INVOKABLE QString getNextTradingDay(const QString& anchorDate);

    Q_INVOKABLE void subscribeRealtime(const QStringList& symbols);
    Q_INVOKABLE void unsubscribeRealtime();

    QVariantList bars() const { return bars_; }
    bool isConnected() const { return false; }

signals:
    void barsChanged();
    void connectedChanged();
    void errorOccurred(const QString& message);

private:
    QVariantList bars_;
    QHash<QString, int> m_subRefCount;
};