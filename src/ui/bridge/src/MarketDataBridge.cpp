#include "MarketDataBridge.h"
#include "../../engine/include/GmSessionEngine.h"

#include <QDateTime>
#include <QDate>
#include "foundation/log/logging.hpp"

namespace bridge {

MarketDataBridge::MarketDataBridge(QObject* parent) : QObject(parent) {}

MarketDataBridge::~MarketDataBridge() = default;

void MarketDataBridge::initialize() {
    if (m_initialized) return;
    m_initialized = true;
    m_connected = true;

    // 从 GmSessionEngine 收实时 tick 推送
    engine::GmSessionEngine::instance().setTickCallback(
        [this](const engine::GmTickData& td) {
            QString qSym = QString::fromStdString(td.symbol);
            auto it = m_marketSnapshots.find(qSym);
            if (it == m_marketSnapshots.end()) {
                updateSnapshot(qSym);
                it = m_marketSnapshots.find(qSym);
                if (it == m_marketSnapshots.end()) return;
            }

            auto snap = it->toMap();
            snap["price"]     = td.price;
            snap["open"]      = td.open;
            snap["high"]      = td.high;
            snap["low"]       = td.low;
            snap["volume"]    = td.lastVolume > 0 ? td.lastVolume : td.cumVolume;
            snap["updatedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
            snap["source"]    = QStringLiteral("掘金推送");

            QVariantList bids, asks;
            for (size_t i = 0; i < (std::min)(td.bidPrices.size(), td.bidVolumes.size()); ++i)
                bids.append(QVariantMap{{"price", td.bidPrices[i]}, {"volume", static_cast<qint64>(td.bidVolumes[i])}});
            for (size_t i = 0; i < (std::min)(td.askPrices.size(), td.askVolumes.size()); ++i)
                asks.append(QVariantMap{{"price", td.askPrices[i]}, {"volume", static_cast<qint64>(td.askVolumes[i])}});
            if (!bids.empty() || !asks.empty())
                snap["depthSnapshot"] = QVariantMap{{"bids", bids}, {"asks", asks}};

            double preClose = snap.value("preClose").toDouble();
            if (preClose > 0) {
                double pct = (td.price - preClose) / preClose * 100.0;
                snap["changePct"] = pct;
                snap["changePercent"] = pct;
            }

            m_marketSnapshots[qSym] = snap;
            emit marketSnapshotsChanged();
        });

    emit initializedChanged();
    emit connectedChanged();
}

void MarketDataBridge::initializeAsync() {
    initialize();
}

void MarketDataBridge::updateSnapshot(const QString& symbol) {
    if (symbol.isEmpty()) return;
    auto q = engine::GmSessionEngine::instance().fetchQuote(symbol.toStdString());
    if (!q || !q->valid) return;

    QVariantMap snap;
    snap["symbol"]     = symbol;
    snap["price"]      = q->price;
    snap["open"]       = q->open;
    snap["high"]       = q->high;
    snap["low"]        = q->low;
    snap["volume"]     = q->volume;
    snap["source"]     = QStringLiteral("实时行情");
    snap["updatedAt"]  = QDateTime::currentDateTime().toString(Qt::ISODate);
    snap["preClose"]      = q->preClose;
    snap["changePct"]      = q->changePct();
    snap["changePercent"]  = QVariant::fromValue(q->changePct());
    snap["limitUp"]        = q->isLimitUp();
    snap["limitDown"]      = q->isLimitDown();
    snap["limitPct"]       = q->limitPct();

    if (q->isLimitUp() && !q->bids.empty()) {
        snap["sealedVolume"] = q->bids[0].volume;
        snap["sealedAmount"] = q->bids[0].volume * q->bids[0].price;
    } else if (q->isLimitDown() && !q->asks.empty()) {
        snap["sealedVolume"] = q->asks[0].volume;
        snap["sealedAmount"] = q->asks[0].volume * q->asks[0].price;
    } else {
        snap["sealedVolume"] = 0.0;
        snap["sealedAmount"] = 0.0;
    }

    QVariantList bids, asks;
    for (auto& b : q->bids) bids.append(QVariantMap{{"price", b.price}, {"volume", static_cast<qint64>(b.volume)}});
    for (auto& a : q->asks) asks.append(QVariantMap{{"price", a.price}, {"volume", static_cast<qint64>(a.volume)}});
    snap["depthSnapshot"] = QVariantMap{{"bids", bids}, {"asks", asks}};

    m_marketSnapshots[symbol] = snap;
    emit marketSnapshotsChanged();
}

void MarketDataBridge::ensureWatchSymbol(const QString& symbol) {
    if (symbol.isEmpty()) return;
    m_trackedSymbols.insert(symbol);
    updateSnapshot(symbol);
    engine::GmSessionEngine::instance().subscribeTick(symbol.toStdString());
}

void MarketDataBridge::activateDefaultWatchlist() {
    if (m_watchlist.isEmpty())
        m_watchlist = QStringList{"000001.SZ", "600000.SH", "600519.SH", "000858.SZ", "601318.SH", "000333.SZ"};
    m_primarySymbol = m_watchlist.first();
    for (const auto& sym : m_watchlist) ensureWatchSymbol(sym);
    emit primarySymbolChanged();
}

QVariantMap MarketDataBridge::resolveInstrument(const QString& symbol) const {
    auto it = m_marketSnapshots.find(symbol);
    if (it != m_marketSnapshots.end()) return it->toMap();

    auto* self = const_cast<MarketDataBridge*>(this);
    self->updateSnapshot(symbol);
    self->m_trackedSymbols.insert(symbol);
    engine::GmSessionEngine::instance().subscribeTick(symbol.toStdString());

    it = m_marketSnapshots.find(symbol);
    if (it != m_marketSnapshots.end()) return it->toMap();

    QVariantMap empty;
    empty["symbol"] = symbol;
    empty["price"]  = 0.0;
    empty["source"] = QStringLiteral("等待行情");
    empty["depthSnapshot"] = QVariantMap{{"bids", QVariantList()}, {"asks", QVariantList()}};
    return empty;
}

void MarketDataBridge::loadBars(const QStringList&, const QString&, const QString&) {}
QVariantMap MarketDataBridge::getCrossSection(const QString&, const QString&, const QStringList&) { return {}; }
QVariantList MarketDataBridge::getIndexConstituents(const QString&, const QString&) { return {}; }
QString MarketDataBridge::getNextTradingDay(const QString& d) {
    QDate dt = QDate::fromString(d, "yyyy-MM-dd"); if (!dt.isValid()) dt = QDate::currentDate();
    do { dt = dt.addDays(1); } while (dt.dayOfWeek() > 5);
    return dt.toString("yyyy-MM-dd");
}

void MarketDataBridge::subscribeRealtime(const QStringList& symbols) {
    for (const auto& s : symbols) {
        m_trackedSymbols.insert(s);
        engine::GmSessionEngine::instance().subscribeTick(s.toStdString());
    }
}

void MarketDataBridge::unsubscribeRealtime() {
    m_trackedSymbols.clear();
}

QVariantMap MarketDataBridge::getTradingStatus(const QString& symbol) const {
    QVariantMap s;
    s["symbol"]    = symbol;
    s["canTrade"]  = true;
    s["status"]    = QStringLiteral("正常");
    s["reason"]    = QString();
    s["isLimitUp"]  = false;
    s["isLimitDown"]= false;
    s["changePct"]  = 0.0;
    s["limitPct"]   = 10.0;

    auto it = m_marketSnapshots.find(symbol);
    if (it == m_marketSnapshots.end()) {
        s["status"] = QStringLiteral("无行情");
        return s;
    }

    auto snap = it->toMap();
    bool limitUp  = snap.value("limitUp").toBool();
    bool limitDown= snap.value("limitDown").toBool();
    double chg    = snap.value("changePct").toDouble();

    s["changePct"] = chg;
    s["limitPct"]  = snap.value("limitPct").toDouble();
    s["isLimitUp"] = limitUp;
    s["isLimitDown"]= limitDown;

    if (limitUp) {
        s["status"] = QStringLiteral("涨停");
        s["reason"] = QString("涨幅 %1%").arg(chg, 0, 'f', 2);
    } else if (limitDown) {
        s["status"] = QStringLiteral("跌停");
        s["reason"] = QString("跌幅 %1%").arg(chg, 0, 'f', 2);
    }

    return s;
}

} // namespace bridge
