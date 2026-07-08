#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QSet>

#include "CandleDataModel.h"
#include "foundation/Utils/Uuid.h"

namespace engine { struct EventFormat; }

namespace bridge {

class MarketDataBridge : public QObject {
    Q_OBJECT
    // ── MarketData 属性 ──
    Q_PROPERTY(bool initialized READ initialized NOTIFY initializedChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(QString primarySymbol READ primarySymbol NOTIFY primarySymbolChanged)
    Q_PROPERTY(QVariantMap marketSnapshots READ marketSnapshots NOTIFY marketSnapshotsChanged)
    Q_PROPERTY(QVariantList bars READ bars NOTIFY barsChanged)
    // ── StockDataLoader 属性 ──
    Q_PROPERTY(bridge::CandleDataModel* model READ model CONSTANT)
    Q_PROPERTY(QString symbol READ symbol WRITE setSymbol NOTIFY symbolChanged)
    Q_PROPERTY(int period READ period WRITE setPeriod NOTIFY periodChanged)
    Q_PROPERTY(QVariantList sectorHeatData READ sectorHeatData NOTIFY sectorHeatDataChanged)

public:
    // ── 周期枚举 (来自 StockDataLoader) ──
    enum Period { TimeShare = 0, Min1 = 1, Min5 = 2, Min15 = 3, Min30 = 4, Min60 = 5, Min120 = 6,
                  Daily = 7, Weekly = 8, Monthly = 9 };
    Q_ENUM(Period)

    explicit MarketDataBridge(QObject* parent = nullptr);
    ~MarketDataBridge() override;

    // ── MarketData 访问器 ──
    bool initialized() const { return m_initialized; }
    bool isConnected() const { return m_connected; }
    QString primarySymbol() const { return m_primarySymbol; }
    QVariantMap marketSnapshots() const { return m_marketSnapshots; }
    QVariantList bars() const { return m_bars; }

    // ── StockDataLoader 访问器 ──
    bridge::CandleDataModel* model() const { return m_model; }
    void setModel(bridge::CandleDataModel* m) { m_model = m; }
    QString symbol() const { return m_symbol; }
    void setSymbol(const QString& s);
    int period() const { return m_period; }
    void setPeriod(int p);

    // ── MarketData Q_INVOKABLE ──
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

    // ── StockDataLoader Q_INVOKABLE ──
    Q_INVOKABLE void loadHistory(const QString& code, int period);
    Q_INVOKABLE void loadFromDB(const QString& code, int period);

    // ── 板块数据 ──
    Q_INVOKABLE void fetchSectorHeat();
    QVariantList sectorHeatData() const { return m_sectorHeatData; }

    // ── 盘后数据同步 ──
    Q_INVOKABLE QString forceSyncToday();
    Q_INVOKABLE QString forceSyncDate(int tradingDay);  // tradingDay 格式: YYYYMMDD
    Q_INVOKABLE QString forceSyncMissingDays(int lookbackDays = 30);  // 自动补齐近N天缺口
    Q_INVOKABLE QString probeGmCoverage(const QString& symbol, const QVariantList& dates);  // 探测掘金覆盖
    Q_INVOKABLE QString fillAdjFactors();  // 补全复权因子

    // ── Domain 工具方法 ──
    Q_INVOKABLE int priceDigitsForMode(const QString& mode) const;
    Q_INVOKABLE double boardLimitRatio(const QString& symbol) const;
    Q_INVOKABLE bool hasRealtimeQuote(const QString& source, const QString& updatedAt) const;
    Q_INVOKABLE bool hasSnapshotQuote(const QString& source, const QString& updatedAt) const;
    Q_INVOKABLE QString invalidSymbolMessageForMode(const QString& mode) const;

signals:
    // ── MarketData 信号 ──
    void initializedChanged();
    void connectedChanged();
    void primarySymbolChanged();
    void marketSnapshotsChanged();
    void barsChanged();
    void errorOccurred(const QString& message);
    // ── StockDataLoader 信号 ──
    void symbolChanged();
    void periodChanged();
    void dataReady();
    void tickReceived(const QString& symbol, double price, double volume);
    void sectorHeatDataChanged();

private:
    // ── tick 事件处理 ──
    void onTickEvent(const engine::EventFormat& event);
    void processTick(const QString& symbol);
    void updateSnapshot(const QString& symbol);

    // ── K线同步 (来自 StockDataLoader) ──
    void syncLiveData();
    void resetSyncState();
    static qint64 periodMs(int period);

    // ── MarketData 状态 ──
    bool m_initialized{false};
    bool m_connected{false};
    QString m_primarySymbol;
    QVariantMap m_marketSnapshots;
    QVariantList m_bars;
    QStringList m_watchlist;
    QSet<QString> m_trackedSymbols;
    foundation::utils::Uuid m_tickSub;

    // ── StockDataLoader 状态 ──
    CandleDataModel* m_model = nullptr;
    QString m_symbol;
    int m_period = Daily;

    // 增量同步状态
    bool   m_isFirstSync = true;
    int    m_modelCount = 0;
    qint64 m_lastBucketKey = -1;
    double m_lastClose = 0.0;
    double m_lastVolume = 0.0;
    double m_lastHigh = 0.0;
    double m_lastLow = 0.0;

    // 快照防抖
    double m_lastSnapPrice = 0.0;
    int    m_lastSnapDepthHash = 0;

    QVariantList m_sectorHeatData;
};

} // namespace bridge
