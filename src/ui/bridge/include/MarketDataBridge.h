#pragma once

#include <QObject>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QSet>

namespace bridge {

class MarketDataBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ initialized NOTIFY initializedChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(QString primarySymbol READ primarySymbol NOTIFY primarySymbolChanged)
    Q_PROPERTY(QVariantMap marketSnapshots READ marketSnapshots NOTIFY marketSnapshotsChanged)
public:
    explicit MarketDataBridge(QObject* parent = nullptr);

    bool initialized() const { return m_initialized; }
    bool isConnected() const { return m_connected; }
    QString primarySymbol() const { return m_primarySymbol; }
    QVariantMap marketSnapshots() const { return m_marketSnapshots; }

    Q_INVOKABLE void initialize();
    Q_INVOKABLE void initializeAsync();
    Q_INVOKABLE void activateDefaultWatchlist();
    Q_INVOKABLE void ensureWatchSymbol(const QString& symbol);
    Q_INVOKABLE QVariantMap resolveInstrument(const QString& symbol) const;
    Q_INVOKABLE void loadBars(const QStringList& symbols, const QString& startDate, const QString& endDate);
    Q_INVOKABLE QVariantMap getCrossSection(const QString& field, const QString& date, const QStringList& symbols = {});
    Q_INVOKABLE QVariantList getIndexConstituents(const QString& indexSymbol, const QString& snapshotDate);
    Q_INVOKABLE QString getNextTradingDay(const QString& anchorDate);
    Q_INVOKABLE void subscribeRealtime(const QStringList& symbols);
    Q_INVOKABLE void unsubscribeRealtime();
    Q_INVOKABLE QVariantMap getTradingStatus(const QString& symbol) const;

signals:
    void initializedChanged();
    void connectedChanged();
    void primarySymbolChanged();
    void marketSnapshotsChanged();
    void barsChanged();
    void errorOccurred(const QString& message);

private:
    void updateSnapshot(const QString& symbol);
    void publishWatchEnsure(const QString& symbol);

    bool m_initialized{false};
    bool m_connected{false};
    QString m_primarySymbol;
    QVariantMap m_marketSnapshots;
    QStringList m_watchlist;
    QSet<QString> m_pollSymbols;
    QTimer* m_pollTimer = nullptr;
    QHash<QString, int> m_subRefCount;
};

} // namespace bridge
