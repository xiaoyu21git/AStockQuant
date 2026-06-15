#include "MarketDataBridge.h"

#include <QDate>
#include <QDebug>
#include <QTimer>

namespace bridge {

MarketDataBridge::MarketDataBridge(QObject* parent)
    : QObject(parent) {}

// ── 初始化 ──
void MarketDataBridge::initialize() {
    if (m_initialized) return;
    m_initialized = true;
    m_connected = true;
    emit initializedChanged();
    emit connectedChanged();
}

void MarketDataBridge::initializeAsync() {
    QTimer::singleShot(0, this, [this]() { initialize(); });
}

// ── 自选股 ──
void MarketDataBridge::activateDefaultWatchlist() {
    if (m_watchlist.isEmpty()) {
        // MVP 默认自选列表
        m_watchlist = QStringList{
            "000001.SZ", "600000.SH", "600519.SH",
            "000858.SZ", "601318.SH", "000333.SZ"
        };
    }
    m_primarySymbol = m_watchlist.first();
    for (const auto& sym : m_watchlist) {
        m_marketSnapshots[sym] = buildMockSnapshot(sym);
    }
    emit primarySymbolChanged();
    emit marketSnapshotsChanged();
}

void MarketDataBridge::ensureWatchSymbol(const QString& symbol) {
    if (symbol.isEmpty()) return;
    if (!m_marketSnapshots.contains(symbol)) {
        m_marketSnapshots[symbol] = buildMockSnapshot(symbol);
        emit marketSnapshotsChanged();
    }
}

// ── 标的解析 ──
QVariantMap MarketDataBridge::resolveInstrument(const QString& symbol) const {
    if (m_marketSnapshots.contains(symbol)) {
        return m_marketSnapshots[symbol].toMap();
    }
    return buildMockSnapshot(symbol);
}

// ── MVP 模拟行情快照 ──
QVariantMap MarketDataBridge::buildMockSnapshot(const QString& symbol) const {
    QVariantMap snap;
    snap["symbol"]  = symbol;
    snap["name"]    = symbol; // MVP: 无真实名称映射
    snap["price"]   = 10.0;
    snap["change"]  = 0.0;
    snap["preClose"] = 10.0;
    snap["source"]  = QStringLiteral("模拟数据");
    snap["updatedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    // 模拟盘口
    QVariantList bids;
    QVariantMap bid1;
    bid1["price"] = 9.99; bid1["volume"] = 10000;
    bids.append(bid1);
    QVariantList asks;
    QVariantMap ask1;
    ask1["price"] = 10.01; ask1["volume"] = 10000;
    asks.append(ask1);

    QVariantMap depth;
    depth["bids"] = bids;
    depth["asks"] = asks;
    snap["depthSnapshot"] = depth;
    snap["recentTicks"] = QVariantList();
    return snap;
}

// ── 历史数据 ──
void MarketDataBridge::loadBars(const QStringList& symbols,
                                 const QString& startDate,
                                 const QString& endDate) {
    Q_UNUSED(symbols); Q_UNUSED(startDate); Q_UNUSED(endDate);
    emit barsChanged();
}

QVariantMap MarketDataBridge::getCrossSection(const QString& field,
                                                const QString& date,
                                                const QStringList& symbols) {
    Q_UNUSED(field); Q_UNUSED(date); Q_UNUSED(symbols);
    return {};
}

QVariantList MarketDataBridge::getIndexConstituents(const QString& indexSymbol,
                                                      const QString& snapshotDate) {
    Q_UNUSED(indexSymbol); Q_UNUSED(snapshotDate);
    return {};
}

// ── 交易日 ──
QString MarketDataBridge::getNextTradingDay(const QString& anchorDate) {
    QDate date = QDate::fromString(anchorDate, "yyyy-MM-dd");
    if (!date.isValid()) date = QDate::currentDate();
    // 简单跳过周末
    do { date = date.addDays(1); } while (date.dayOfWeek() > 5);
    return date.toString("yyyy-MM-dd");
}

// ── 实时订阅 ──
void MarketDataBridge::subscribeRealtime(const QStringList& symbols) {
    for (const auto& sym : symbols) {
        ++m_subRefCount[sym];
    }
}

void MarketDataBridge::unsubscribeRealtime() {
    m_subRefCount.clear();
}

} // namespace bridge
