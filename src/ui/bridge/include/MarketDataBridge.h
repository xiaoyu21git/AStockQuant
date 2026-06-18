#pragma once
// ═════════════════════════════════════════════════════════════════════════
// MarketDataBridge — 行情数据桥接层
// 提供实时行情快照、标的解析、自选股管理等 QML 接口
// MVP 实现：静态/模拟数据，后续接入实时行情源
// ═════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include "foundation/Utils/Uuid.h"

namespace bridge {

class MarketDataBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ initialized NOTIFY initializedChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(QString primarySymbol READ primarySymbol NOTIFY primarySymbolChanged)
    Q_PROPERTY(QVariantMap marketSnapshots READ marketSnapshots NOTIFY marketSnapshotsChanged)
public:
    explicit MarketDataBridge(QObject* parent = nullptr);

    // ── 属性 ──
    bool initialized() const { return m_initialized; }
    bool isConnected() const { return m_connected; }
    QString primarySymbol() const { return m_primarySymbol; }
    QVariantMap marketSnapshots() const { return m_marketSnapshots; }

    // ── 初始化 ──
    Q_INVOKABLE void initialize();
    Q_INVOKABLE void initializeAsync();

    // ── 自选股 ──
    Q_INVOKABLE void activateDefaultWatchlist();
    Q_INVOKABLE void ensureWatchSymbol(const QString& symbol);

    // ── 标的解析 ──
    /// @return {symbol, name, price, change, preClose, source, updatedAt,
    ///          depthSnapshot: {bids, asks}, recentTicks: [...]}
    Q_INVOKABLE QVariantMap resolveInstrument(const QString& symbol) const;

    // ── 历史数据 ──
    Q_INVOKABLE void loadBars(const QStringList& symbols,
                               const QString& startDate,
                               const QString& endDate);
    Q_INVOKABLE QVariantMap getCrossSection(const QString& field,
                                              const QString& date,
                                              const QStringList& symbols = {});
    Q_INVOKABLE QVariantList getIndexConstituents(const QString& indexSymbol,
                                                    const QString& snapshotDate);

    // ── 交易日 ──
    Q_INVOKABLE QString getNextTradingDay(const QString& anchorDate);

    // ── 实时订阅 ──
    Q_INVOKABLE void subscribeRealtime(const QStringList& symbols);
    Q_INVOKABLE void unsubscribeRealtime();

signals:
    void initializedChanged();
    void connectedChanged();
    void primarySymbolChanged();
    void marketSnapshotsChanged();
    void barsChanged();
    void errorOccurred(const QString& message);

private:
    QVariantMap queryLastTick(const QString& symbol) const;

    bool m_initialized{false};
    bool m_connected{false};
    QString m_primarySymbol;
    QVariantMap m_marketSnapshots;
    QStringList m_watchlist;
    QVariantList m_bars;
    QHash<QString, int> m_subRefCount;
    foundation::utils::Uuid m_tickSub;
};

} // namespace bridge
