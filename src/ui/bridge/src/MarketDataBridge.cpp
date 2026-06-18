#include "MarketDataBridge.h"
#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"
#include "../../../thirdparty/gmsdk/gmapi.h"

#include <QDateTime>
#include <QDebug>
#include <QTimer>

namespace bridge {

MarketDataBridge::MarketDataBridge(QObject* parent) : QObject(parent) {}

void MarketDataBridge::initialize() {
    if (m_initialized) return;
    m_initialized = true;
    m_connected = true;

    auto* bus = engine::get_engine_event_bus();
    if (bus) {
        m_tickSub = bus->subscribe("trading.market.tick",
            [this](const engine::EventFormat& event) {
                auto sym = event.get<std::string>("symbol");
                if (!sym.has_value()) return;
                QString key = QString::fromStdString(*sym);

                QVariantMap snap;
                snap["symbol"]  = key;
                snap["price"]   = event.get<double>("price").value_or(0.0);
                snap["open"]    = event.get<double>("open").value_or(0.0);
                snap["high"]    = event.get<double>("high").value_or(0.0);
                snap["low"]     = event.get<double>("low").value_or(0.0);
                snap["volume"]  = event.get<double>("volume").value_or(0.0);
                snap["source"]  = QStringLiteral("掘金实时");
                snap["updatedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);

                QVariantList bids, asks;
                auto bp = event.get<std::vector<double>>("bid_prices");
                auto bv = event.get<std::vector<double>>("bid_volumes");
                if (bp.has_value() && bv.has_value())
                    for (size_t i = 0; i < bp->size() && i < bv->size(); ++i)
                        bids.append(QVariantMap{{"price", (*bp)[i]}, {"volume", static_cast<qint64>((*bv)[i])}});
                auto ap = event.get<std::vector<double>>("ask_prices");
                auto av = event.get<std::vector<double>>("ask_volumes");
                if (ap.has_value() && av.has_value())
                    for (size_t i = 0; i < ap->size() && i < av->size(); ++i)
                        asks.append(QVariantMap{{"price", (*ap)[i]}, {"volume", static_cast<qint64>((*av)[i])}});
                snap["depthSnapshot"] = QVariantMap{{"bids", bids}, {"asks", asks}};

                m_marketSnapshots[key] = snap;
                static int mdLog = 0;
                if (++mdLog <= 3)
                    qDebug() << "[MktBridge] tick snapshot:" << key << snap["price"].toDouble();
                emit marketSnapshotsChanged();
            });
    }

    emit initializedChanged();
    emit connectedChanged();
}

void MarketDataBridge::initializeAsync() {
    QTimer::singleShot(0, this, [this]() { initialize(); });
}

void MarketDataBridge::activateDefaultWatchlist() {
    if (m_watchlist.isEmpty())
        m_watchlist = QStringList{"000001.SZ", "600000.SH", "600519.SH", "000858.SZ", "601318.SH", "000333.SZ"};
    m_primarySymbol = m_watchlist.first();
    emit primarySymbolChanged();
}

void MarketDataBridge::ensureWatchSymbol(const QString& symbol) {
    if (symbol.isEmpty()) return;

    // 通过 SDK 全局函数直接获取最新快照（收盘后也能查到）
    if (!m_marketSnapshots.contains(symbol)) {
        QVariantMap snap = queryLastTick(symbol);
        if (!snap.isEmpty()) {
            m_marketSnapshots[symbol] = snap;
            emit marketSnapshotsChanged();
        }
    }

    // 同时通过 EventBus 通知 JMC 订阅实时推送
    auto* bus = engine::get_engine_event_bus();
    if (bus && bus->is_running()) {
        engine::EventFormat event = engine::EventFormat::create_from_strings(
            "market.watch.ensure", "MarketDataBridge", 0);
        event.set("symbol", symbol.toStdString());
        bus->publish(event, 0);
    }
}

QVariantMap MarketDataBridge::queryLastTick(const QString& symbol) const {
    QVariantMap snap;

    QString s = symbol.toUpper();
    int dot = s.indexOf('.');
    if (dot <= 0) return snap;
    QString code = s.left(dot), exchange = s.mid(dot + 1);
    QString gmSym;
    if (exchange == "SH") gmSym = "SHSE." + code;
    else if (exchange == "SZ") gmSym = "SZSE." + code;
    else if (exchange == "BJ") gmSym = "BSE." + code;
    else return snap;

    auto gs = gmSym.toStdString();

    // 开盘时用 current()，收盘/非交易日回退 last_tick()
    auto* arr = ::current(gs.c_str(), false);
    const char* source = "实时快照";
    if (!arr || arr->status() != 0 || arr->count() == 0) {
        if (arr) arr->release();
        arr = ::last_tick(gs.c_str(), false);
        source = "盘后快照";
    }
    if (!arr || arr->status() != 0 || arr->count() == 0) {
        if (arr) arr->release();
        return snap;
    }

    auto& tick = arr->at(0);
    snap["symbol"]    = symbol;
    snap["price"]     = static_cast<double>(tick.price);
    snap["open"]      = static_cast<double>(tick.open);
    snap["high"]      = static_cast<double>(tick.high);
    snap["low"]       = static_cast<double>(tick.low);
    snap["volume"]    = tick.cum_volume;
    snap["source"]    = QString::fromLatin1(source);
    snap["updatedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QVariantList bids, asks;
    for (int i = 0; i < DEPTH_OF_QUOTE && i < 5; ++i) {
        auto& q = tick.quotes[i];
        if (q.bid_price > 0)
            bids.append(QVariantMap{{"price", static_cast<double>(q.bid_price)},
                                    {"volume", static_cast<qint64>(q.bid_volume)}});
        if (q.ask_price > 0)
            asks.append(QVariantMap{{"price", static_cast<double>(q.ask_price)},
                                    {"volume", static_cast<qint64>(q.ask_volume)}});
    }
    snap["depthSnapshot"] = QVariantMap{{"bids", bids}, {"asks", asks}};
    arr->release();
    return snap;
}

QVariantMap MarketDataBridge::resolveInstrument(const QString& symbol) const {
    if (m_marketSnapshots.contains(symbol))
        return m_marketSnapshots[symbol].toMap();
    QVariantMap empty;
    empty["symbol"] = symbol;
    empty["price"]  = 0.0;
    empty["source"] = QStringLiteral("等待行情");
    empty["depthSnapshot"] = QVariantMap{{"bids", QVariantList()}, {"asks", QVariantList()}};
    return empty;
}

void MarketDataBridge::loadBars(const QStringList& s, const QString& sd, const QString& ed) {
    Q_UNUSED(s); Q_UNUSED(sd); Q_UNUSED(ed); emit barsChanged();
}

QVariantMap MarketDataBridge::getCrossSection(const QString& a, const QString& b, const QStringList& c) {
    Q_UNUSED(a); Q_UNUSED(b); Q_UNUSED(c); return {};
}

QVariantList MarketDataBridge::getIndexConstituents(const QString& a, const QString& b) {
    Q_UNUSED(a); Q_UNUSED(b); return {};
}

QString MarketDataBridge::getNextTradingDay(const QString& anchorDate) {
    QDate date = QDate::fromString(anchorDate, "yyyy-MM-dd");
    if (!date.isValid()) date = QDate::currentDate();
    do { date = date.addDays(1); } while (date.dayOfWeek() > 5);
    return date.toString("yyyy-MM-dd");
}

void MarketDataBridge::subscribeRealtime(const QStringList& symbols) {
    for (const auto& sym : symbols) ++m_subRefCount[sym];
}

void MarketDataBridge::unsubscribeRealtime() { m_subRefCount.clear(); }

} // namespace bridge
