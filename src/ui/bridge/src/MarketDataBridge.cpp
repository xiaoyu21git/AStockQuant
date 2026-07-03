// MarketDataBridge.cpp — 统一行情桥接层
// tick 事件驱动 (被动推送), 同时处理行情快照 + K线聚合, 零定时器
#include "MarketDataBridge.h"
#include "../../../domain/market/include/MarketDataService.h"
#include "../../../domain/market/include/LiveData.h"
#include "../../engine/include/GmSessionEngine.h"
#include "../../../domain/trading/include/MarketDataUtils.h"
#include "../../../infrastructure/include/database/MarketDataRepository.h"
#include "../../../infrastructure/include/database/NativeMySQLConnectionPool.h"
#include "../../engine/include/GlobalEventBusRegistry.h"
#include "../../../infrastructure/include/database/PostMarketSyncService.h"
#include "../../../thirdparty/gmsdk/strategy.h"
#include "foundation/market/AStockSymbol.h"

#include <QDateTime>
#include <QDate>
#include <map>
#include <vector>
#include <cmath>
#include "foundation/log/logging.hpp"

namespace bridge {

MarketDataBridge::MarketDataBridge(QObject* parent) : QObject(parent) {}

MarketDataBridge::~MarketDataBridge() {
    if (m_tickSub.is_valid()) {
        auto* bus = engine::get_engine_event_bus();
        if (bus) bus->unsubscribe(m_tickSub);
    }
}

void MarketDataBridge::initialize() {
    if (m_initialized) return;
    m_initialized = true;
    m_connected = true;

    // 订阅 EventBus tick 事件 — 被动通知。
    // 注意: GmSessionEngine::on_tick() 先 publish 后调 MarketDataService::onTick(),
    // 但桥接层用 Qt::QueuedConnection 延迟到主线程处理, gmsdk 线程已跑完 onTick(),
    // LiveData 保证是最新的。
    auto* bus = engine::get_engine_event_bus();
    if (bus && bus->is_running()) {
        m_tickSub = bus->subscribe("trading.market.tick",
            [this](const engine::EventFormat& evt) { onTickEvent(evt); });
    }

    emit initializedChanged();
    emit connectedChanged();
}

void MarketDataBridge::initializeAsync() {
    initialize();
}

// ═══════════════════════════════════════════════════════════════════
// tick 事件处理
// ═══════════════════════════════════════════════════════════════════

void MarketDataBridge::onTickEvent(const engine::EventFormat& event) {
    // gmsdk 线程回调 — 仅提取 symbol, 全部逻辑 marshal 到 Qt 主线程
    auto symbol = event.get<std::string>("symbol");
    if (!symbol.has_value()) return;
    QString sym = QString::fromStdString(*symbol);
    QMetaObject::invokeMethod(this, [this, sym]() {
        processTick(sym);
    }, Qt::QueuedConnection);
}

void MarketDataBridge::processTick(const QString& symbol) {
    // Qt 主线程执行。此时 MarketDataService::onTick() 已跑完, LiveData 是最新的。
    bool isTracked = m_trackedSymbols.contains(symbol);
    bool isChart   = (m_symbol == symbol);

    if (!isTracked && !isChart) return;

    // 1) 更新行情快照 (价格 + 五档盘口)
    if (isTracked) {
        updateSnapshot(symbol);
    }

    // 2) 更新 K 线模型 (仅日内周期, 图表标的)
    if (isChart && m_model &&
        (m_period == TimeShare || (m_period >= Min1 && m_period <= Min120))) {
        syncLiveData();
    }
}

// ═══════════════════════════════════════════════════════════════════
// 行情快照
// ═══════════════════════════════════════════════════════════════════

void MarketDataBridge::updateSnapshot(const QString& symbol) {
    if (symbol.isEmpty()) return;
    std::string sym = symbol.toStdString();
    if (sym.find('.') == std::string::npos && sym.size() == 6) {
        auto symObj = foundation::market::AStockSymbol::fromCode(sym);
        if (symObj.isValid()) sym = symObj.fullSymbol();
    }

    auto& d = domain::market::MarketDataService::instance().liveData(sym);
    if (!d.valid() || d.dailyBar().close() <= 0.0) {
        // 内存无数据 → gmsdk fetchQuote 回退
        auto q = engine::GmSessionEngine::instance().fetchQuote(sym);
        if (q.has_value() && q->valid && q->price > 0) {
            QVariantMap snap;
            snap["symbol"]    = QString::fromStdString(sym);
            snap["price"]     = q->price;
            snap["open"]      = q->open;
            snap["high"]      = q->high;
            snap["low"]       = q->low;
            snap["volume"]    = q->volume;
            snap["preClose"]  = q->preClose;
            snap["changePct"] = q->changePct();
            snap["changePercent"] = QVariant::fromValue(q->changePct());

            // 五档深度
            QVariantMap depthSnap;
            QVariantList bids, asks;
            double totalBid = 0, totalAsk = 0;
            for (const auto& lv : q->bids) {
                QVariantMap m; m["price"] = lv.price; m["volume"] = lv.volume;
                bids.append(m); totalBid += lv.volume;
            }
            for (const auto& lv : q->asks) {
                QVariantMap m; m["price"] = lv.price; m["volume"] = lv.volume;
                asks.append(m); totalAsk += lv.volume;
            }
            depthSnap["bids"] = bids; depthSnap["asks"] = asks;
            depthSnap["totalBid"] = totalBid; depthSnap["totalAsk"] = totalAsk;
            depthSnap["live"] = false;
            snap["depthSnapshot"] = depthSnap;

            snap["source"]    = QStringLiteral("gmsdk快照");
            snap["updatedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
            m_marketSnapshots[symbol] = snap;
            m_marketSnapshots = QVariantMap(m_marketSnapshots);
            emit marketSnapshotsChanged();
            return;
        }
        return;
    }

    const auto& bar = d.dailyBar();
    const auto& depth = d.depth();
    double pc = d.preClose();

    // ── 防抖: 价格和深度都没变则跳过 ──
    double newPrice = bar.close();
    int newDepthHash = 0;
    for (int i = 0; i < depth.levelCount(); ++i) {
        newDepthHash ^= static_cast<int>(depth.bidPrice(i) * 1000) ^ static_cast<int>(depth.askPrice(i) * 1000);
    }
    if (newPrice == m_lastSnapPrice && newDepthHash == m_lastSnapDepthHash) return;
    m_lastSnapPrice = newPrice;
    m_lastSnapDepthHash = newDepthHash;

    QVariantMap snap;
    snap["symbol"]     = QString::fromStdString(sym);
    snap["price"]      = bar.close();
    snap["open"]       = bar.open();
    snap["high"]       = bar.high();
    snap["low"]        = bar.low();
    snap["volume"]     = bar.volume();
    snap["amount"]     = bar.amount();
    snap["avgLine"]    = d.avgLine();
    snap["source"]     = QStringLiteral("实时行情");
    snap["updatedAt"]  = QDateTime::currentDateTime().toString(Qt::ISODate);
    snap["preClose"]      = pc;
    double changePct = (pc > 0.0) ? (bar.close() - pc) / pc * 100.0 : 0.0;
    snap["changePct"]      = changePct;
    snap["changePercent"]  = QVariant::fromValue(changePct);
    double limitPct = d.limitUp() > 0.0 && pc > 0.0 ? (d.limitUp() / pc - 1.0) * 100.0 : 10.0;
    bool isLimitUp   = d.limitUp()   > 0.0 && bar.close() >= d.limitUp();
    bool isLimitDown = d.limitDown() > 0.0 && bar.close() <= d.limitDown();
    snap["limitUp"]        = isLimitUp;
    snap["limitDown"]      = isLimitDown;
    snap["limitPct"]       = limitPct;

    // 封单量
    if (isLimitUp && depth.levelCount() > 0) {
        snap["sealedVolume"] = depth.bidVolume(0);
        snap["sealedAmount"] = depth.bidVolume(0) * depth.bidPrice(0);
    } else if (isLimitDown && depth.levelCount() > 0) {
        snap["sealedVolume"] = depth.askVolume(0);
        snap["sealedAmount"] = depth.askVolume(0) * depth.askPrice(0);
    } else {
        snap["sealedVolume"] = 0.0;
        snap["sealedAmount"] = 0.0;
    }

    QVariantList bids, asks;
    for (int i = 0; i < depth.levelCount(); ++i)
        bids.append(QVariantMap{{"price", depth.bidPrice(i)}, {"volume", static_cast<qint64>(depth.bidVolume(i))}});
    for (int i = 0; i < depth.levelCount(); ++i)
        asks.append(QVariantMap{{"price", depth.askPrice(i)}, {"volume", static_cast<qint64>(depth.askVolume(i))}});
    snap["depthSnapshot"] = QVariantMap{{"bids", bids}, {"asks", asks}};

    m_marketSnapshots[symbol] = snap;
    m_marketSnapshots = QVariantMap(m_marketSnapshots);
    emit marketSnapshotsChanged();
}

// ═══════════════════════════════════════════════════════════════════
// 标的管理
// ═══════════════════════════════════════════════════════════════════

void MarketDataBridge::ensureWatchSymbol(const QString& symbol) {
    if (symbol.isEmpty()) return;

    QString resolved = symbol;
    std::string sym = symbol.toStdString();
    if (sym.find('.') == std::string::npos && sym.size() == 6) {
        auto symObj = foundation::market::AStockSymbol::fromCode(sym);
        if (symObj.isValid()) resolved = QString::fromStdString(symObj.fullSymbol());
    }

    m_trackedSymbols.insert(resolved);
    updateSnapshot(resolved);
    if (m_primarySymbol != resolved) {
        m_primarySymbol = resolved;
        emit primarySymbolChanged();
    }
}

void MarketDataBridge::activateDefaultWatchlist() {
    if (m_watchlist.isEmpty())
        m_watchlist = QStringList{"000001.SZ", "600000.SH", "600519.SH", "000858.SZ", "601318.SH", "000333.SZ"};
    m_primarySymbol = m_watchlist.first();
    for (const auto& sym : m_watchlist) ensureWatchSymbol(sym);
    emit primarySymbolChanged();
}

QVariantMap MarketDataBridge::resolveInstrument(const QString& symbol) const {
    QString resolved = symbol;
    std::string sym = symbol.toStdString();
    if (sym.find('.') == std::string::npos && sym.size() == 6) {
        auto symObj = foundation::market::AStockSymbol::fromCode(sym);
        if (symObj.isValid()) resolved = QString::fromStdString(symObj.fullSymbol());
    }

    auto it = m_marketSnapshots.find(resolved);
    if (it != m_marketSnapshots.end()) return it->toMap();

    auto* self = const_cast<MarketDataBridge*>(this);
    self->updateSnapshot(resolved);
    self->m_trackedSymbols.insert(resolved);

    it = m_marketSnapshots.find(resolved);
    if (it != m_marketSnapshots.end()) return it->toMap();

    QVariantMap empty;
    empty["symbol"] = symbol;
    empty["price"]  = 0.0;
    empty["source"] = QStringLiteral("等待行情");
    empty["depthSnapshot"] = QVariantMap{{"bids", QVariantList()}, {"asks", QVariantList()}};
    return empty;
}

void MarketDataBridge::loadBars(const QStringList& symbols, const QString& startDate, const QString& endDate) {
    if (symbols.isEmpty()) return;

    auto db = astock::database::NativeMySQLConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) return;

    astock::infrastructure::database::MarketDataRepository repo(std::move(db));
    QVariantList result;

    for (const auto& sym : symbols) {
        if (sym.trimmed().isEmpty()) continue;
        auto rows = repo.queryDailyBar(
            sym.trimmed().toStdString(),
            startDate.toStdString(),
            endDate.toStdString());

        for (const auto& row : rows) {
            QVariantMap item;
            item["symbol"] = QString::fromStdString(row.symbol);
            item["date"]   = QString::fromStdString(row.tradeDate);
            item["time"]   = item["date"];
            item["open"]   = row.open;
            item["high"]   = row.high;
            item["low"]    = row.low;
            item["close"]  = row.close;
            item["volume"] = row.volume;
            result.append(item);
        }
    }

    m_bars = result;
    emit barsChanged();
}

QVariantMap MarketDataBridge::getCrossSection(const QString&, const QString&, const QStringList&) { return {}; }
QVariantList MarketDataBridge::getIndexConstituents(const QString&, const QString&) { return {}; }
QString MarketDataBridge::getNextTradingDay(const QString& d) {
    QDate dt = QDate::fromString(d, "yyyy-MM-dd"); if (!dt.isValid()) dt = QDate::currentDate();
    do { dt = dt.addDays(1); } while (dt.dayOfWeek() > 5);
    return dt.toString("yyyy-MM-dd");
}

void MarketDataBridge::subscribeRealtime(const QStringList& symbols) {
    for (const auto& s : symbols) {
        QString resolved = s;
        std::string sym = s.toStdString();
        if (sym.find('.') == std::string::npos && sym.size() == 6) {
            auto symObj = foundation::market::AStockSymbol::fromCode(sym);
            if (symObj.isValid()) resolved = QString::fromStdString(symObj.fullSymbol());
        }
        m_trackedSymbols.insert(resolved);
        engine::GmSessionEngine::instance().subscribeTick(resolved.toStdString());
    }
}

void MarketDataBridge::unsubscribeRealtime() {
    for (const auto& s : m_trackedSymbols)
        engine::GmSessionEngine::instance().unsubscribeTick(s.toStdString());
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

// ═══════════════════════════════════════════════════════════════════
// Domain 工具方法
// ═══════════════════════════════════════════════════════════════════

int MarketDataBridge::priceDigitsForMode(const QString& mode) const {
    return domain::trading::priceDigitsForMode(mode.toStdString());
}

double MarketDataBridge::boardLimitRatio(const QString& symbol) const {
    return domain::trading::boardLimitRatio(symbol.toStdString());
}

bool MarketDataBridge::hasRealtimeQuote(const QString& source, const QString& updatedAt) const {
    return domain::trading::hasRealtimeQuote(source.toStdString(), updatedAt.toStdString());
}

bool MarketDataBridge::hasSnapshotQuote(const QString& source, const QString& updatedAt) const {
    return domain::trading::hasSnapshotQuote(source.toStdString(), updatedAt.toStdString());
}

QString MarketDataBridge::invalidSymbolMessageForMode(const QString& mode) const {
    return QString::fromUtf8(domain::trading::invalidSymbolMessageForMode(mode.toStdString()));
}

// ═══════════════════════════════════════════════════════════════════
// K线数据加载 (来自原 StockDataLoader)
// ═══════════════════════════════════════════════════════════════════

void MarketDataBridge::setSymbol(const QString& s) {
    if (m_symbol != s) {
        m_symbol = s;
        if (m_model) m_model->clear();
        resetSyncState();
        emit symbolChanged();
    }
}

void MarketDataBridge::setPeriod(int p) {
    if (m_period != p) {
        m_period = p;
        resetSyncState();
        emit periodChanged();
    }
}

void MarketDataBridge::resetSyncState() {
    m_isFirstSync = true;
    m_modelCount = 0;
    m_lastBucketKey = -1;
    m_lastClose = 0.0;
    m_lastVolume = 0.0;
    m_lastHigh = 0.0;
    m_lastLow = 0.0;
}

qint64 MarketDataBridge::periodMs(int period) {
    switch (period) {
        case TimeShare: return 60'000;
        case Min1:   return 60'000;
        case Min5:   return 5 * 60'000LL;
        case Min15:  return 15 * 60'000LL;
        case Min30:  return 30 * 60'000LL;
        case Min60:  return 60 * 60'000LL;
        case Min120: return 120 * 60'000LL;
        default:     return 60'000;
    }
}

// ── 工具函数 ──
static QVariantMap barToMap(const domain::market::Bar& b) {
    QVariantMap m;
    m["timestamp"] = QVariant::fromValue<qint64>(b.timeBegin());
    m["open"]   = b.open();
    m["high"]   = b.high();
    m["low"]    = b.low();
    m["close"]  = b.close();
    m["volume"] = b.volume();
    return m;
}

static QVariantList aggregateWeekly(const QVariantList& daily) {
    std::map<int, std::vector<QVariantMap>> groups;
    for (const auto& d : daily) {
        auto m = d.toMap();
        auto dt = QDateTime::fromMSecsSinceEpoch(m["timestamp"].toLongLong());
        int weekKey = dt.date().year() * 100 + dt.date().weekNumber();
        groups[weekKey].push_back(m);
    }
    QVariantList result;
    for (auto& [key, bars] : groups) {
        double o  = bars.front()["open"].toDouble();
        double c  = bars.back()["close"].toDouble();
        double hi = 0, lo = 1e18; double vol = 0;
        qint64 ts = bars.front()["timestamp"].toLongLong();
        for (auto& b : bars) {
            double h = b["high"].toDouble(), l = b["low"].toDouble();
            if (h > hi) hi = h; if (l < lo) lo = l;
            vol += b["volume"].toDouble();
        }
        QVariantMap item;
        item["timestamp"] = ts; item["open"] = o; item["high"] = hi;
        item["low"] = lo;     item["close"] = c; item["volume"] = vol;
        result.append(item);
    }
    return result;
}

static QVariantList aggregateMonthly(const QVariantList& daily) {
    std::map<int, std::vector<QVariantMap>> groups;
    for (const auto& d : daily) {
        auto m = d.toMap();
        auto dt = QDateTime::fromMSecsSinceEpoch(m["timestamp"].toLongLong());
        int monKey = dt.date().year() * 100 + dt.date().month();
        groups[monKey].push_back(m);
    }
    QVariantList result;
    for (auto& [key, bars] : groups) {
        double o  = bars.front()["open"].toDouble();
        double c  = bars.back()["close"].toDouble();
        double hi = 0, lo = 1e18; double vol = 0;
        qint64 ts = bars.front()["timestamp"].toLongLong();
        for (auto& b : bars) {
            double h = b["high"].toDouble(), l = b["low"].toDouble();
            if (h > hi) hi = h; if (l < lo) lo = l;
            vol += b["volume"].toDouble();
        }
        QVariantMap item;
        item["timestamp"] = ts; item["open"] = o; item["high"] = hi;
        item["low"] = lo;     item["close"] = c; item["volume"] = vol;
        result.append(item);
    }
    return result;
}

static QVariantList loadDailyBars(const std::string& gmSym, int lookback) {
    auto now = std::chrono::system_clock::now();
    auto start = now - std::chrono::hours(24 * lookback);
    auto t_now  = std::chrono::system_clock::to_time_t(now);
    auto t_start = std::chrono::system_clock::to_time_t(start);
    char s[32], e[32];
    std::strftime(s, sizeof(s), "%Y-%m-%d", std::localtime(&t_start));
    std::strftime(e, sizeof(e), "%Y-%m-%d", std::localtime(&t_now));

    auto* bars = ::history_bars(gmSym.c_str(), "1d", s, e, 0, nullptr, true, nullptr);
    QVariantList list;
    if (!bars || bars->status() || !bars->count()) {
        if (bars) bars->release();
        return list;
    }
    for (size_t i = 0; i < bars->count(); ++i) {
        auto& b = bars->at(i);
        QVariantMap item;
        item["timestamp"] = QVariant::fromValue<qint64>(static_cast<qint64>(b.bob * 1000.0));
        item["open"]   = static_cast<double>(b.open);
        item["high"]   = static_cast<double>(b.high);
        item["low"]    = static_cast<double>(b.low);
        item["close"]  = static_cast<double>(b.close);
        item["volume"] = b.volume;
        list.append(item);
    }
    bars->release();
    return list;
}

void MarketDataBridge::loadHistory(const QString& code, int period) {
    if (code.isEmpty()) return;
    std::string sym = code.toStdString();
    if (sym.find('.') == std::string::npos && sym.size() == 6) {
        auto obj = foundation::market::AStockSymbol::fromCode(sym);
        if (obj.isValid()) sym = obj.fullSymbol();
    }
    m_symbol = QString::fromStdString(sym);
    m_period = period;
    resetSyncState();
    loadFromDB(code, period);
}

void MarketDataBridge::loadFromDB(const QString& code, int period) {
    if (!m_model) return;
    std::string sym = code.toStdString();
    if (sym.find('.') == std::string::npos && sym.size() == 6) {
        auto obj = foundation::market::AStockSymbol::fromCode(sym);
        if (obj.isValid()) sym = obj.fullSymbol();
    }
    m_symbol = QString::fromStdString(sym);
    m_period = period;
    resetSyncState();

    std::string gmSym = engine::GmSessionEngine::toGmSymbol(sym);
    if (gmSym.empty()) return;

    QVariantList result;

    if (period == TimeShare || (period >= Min1 && period <= Min120)) {
        // 日内周期: 不再启停定时器, tick 事件驱动同步
        const char* freq = "60s";
        int lookbackDays = 0;
        if (period == Min5) freq = "300s";
        else if (period == Min15) freq = "900s";
        else if (period == Min30) freq = "1800s";
        else if (period == Min60) freq = "3600s";
        else if (period == Min120) freq = "7200s";
        else if (period == TimeShare) freq = "60s";
        auto now2 = std::chrono::system_clock::now();
        auto start2 = now2 - std::chrono::hours(24 * lookbackDays);
        auto t_now2  = std::chrono::system_clock::to_time_t(now2);
        auto t_start2 = std::chrono::system_clock::to_time_t(start2);
        char s2[32], e2[32];
        std::strftime(s2, sizeof(s2), "%Y-%m-%d %H:%M:%S", std::localtime(&t_start2));
        std::strftime(e2, sizeof(e2), "%Y-%m-%d %H:%M:%S", std::localtime(&t_now2));
        auto* minBars = ::history_bars(gmSym.c_str(), freq, s2, e2, 0, nullptr, true, nullptr);
        if (minBars && !minBars->status() && minBars->count() > 0) {
            for (size_t i = 0; i < minBars->count(); ++i) {
                auto& b = minBars->at(i);
                QVariantMap item;
                item["timestamp"] = QVariant::fromValue<qint64>(static_cast<qint64>(b.bob * 1000.0));
                item["open"]   = b.open;
                item["high"]   = b.high;
                item["low"]    = b.low;
                item["close"]  = b.close;
                item["volume"] = b.volume;
                result.append(item);
            }
        }
        if (minBars) minBars->release();
        if (result.isEmpty() && (period == TimeShare || (period >= Min1 && period <= Min120))) {
            auto now3 = std::chrono::system_clock::now();
            auto start3 = now3 - std::chrono::hours(24 * 5);
            auto t_now3  = std::chrono::system_clock::to_time_t(now3);
            auto t_start3 = std::chrono::system_clock::to_time_t(start3);
            char s3[32], e3[32];
            std::strftime(s3, sizeof(s3), "%Y-%m-%d %H:%M:%S", std::localtime(&t_start3));
            std::strftime(e3, sizeof(e3), "%Y-%m-%d %H:%M:%S", std::localtime(&t_now3));
            auto* fallback = ::history_bars(gmSym.c_str(), "60s", s3, e3, 0, nullptr, true, nullptr);
            if (fallback && !fallback->status() && fallback->count() > 0) {
                qint64 lastDay = 0;
                std::vector<QVariantMap> dayBars;
                for (size_t i = 0; i < fallback->count(); ++i) {
                    auto& b = fallback->at(i);
                    qint64 ts = static_cast<qint64>(b.bob * 1000.0);
                    qint64 day = ts / (24 * 3600 * 1000);
                    if (lastDay == 0) lastDay = day;
                    if (day != lastDay) { dayBars.clear(); lastDay = day; }
                    QVariantMap item;
                    item["timestamp"] = QVariant::fromValue<qint64>(ts);
                    item["open"]=b.open; item["high"]=b.high; item["low"]=b.low;
                    item["close"]=b.close; item["volume"]=b.volume;
                    dayBars.push_back(item);
                }
                for (auto& m : dayBars) result.append(m);
            }
            if (fallback) fallback->release();
        }
        if (result.isEmpty() && !(period == TimeShare || (period >= Min1 && period <= Min120))) {
            auto daily = loadDailyBars(gmSym, 5);
            double pc = daily.isEmpty() ? 0.0 : daily.last().toMap()["close"].toDouble();
            if (pc > 0) {
                QVariantMap m;
                m["timestamp"] = QVariant::fromValue<qint64>(QDateTime::currentDateTime().toMSecsSinceEpoch());
                m["open"]=pc; m["high"]=pc; m["low"]=pc; m["close"]=pc; m["volume"]=0;
                result.append(m);
            }
        }
    } else {
        auto daily = loadDailyBars(gmSym, 500);
        if (daily.isEmpty()) return;
        switch (period) {
            case Weekly:  result = aggregateWeekly(daily);  break;
            case Monthly: result = aggregateMonthly(daily); break;
            default:      result = std::move(daily);        break;
        }
    }

    m_model->setCandles(result);
    m_model->setPreClose(result.isEmpty() ? 0.0 : result.last().toMap()["close"].toDouble());
    if (period == TimeShare || (period >= Min1 && period <= Min120)) {
        double tv = 0.0, tvol = 0.0;
        for (const auto& r : result) {
            auto m = r.toMap();
            double v = m["volume"].toDouble();
            if (v > 0) { tv += m["close"].toDouble() * v; tvol += v; }
        }
        m_model->setAvgLine(tvol > 0 ? tv / tvol : 0.0);
    }
    m_modelCount = result.size();
    m_isFirstSync = false;
    emit dataReady();
}

// ═══════════════════════════════════════════════════════════════════
// 实时 K 线同步 (来自原 StockDataLoader::syncLiveData, tick 驱动)
// ═══════════════════════════════════════════════════════════════════

struct AggBucket {
    qint64 bucketStart = 0;
    double o = 0.0, h = 0.0, l = 0.0, c = 0.0, v = 0.0;

    QVariantMap toMap() const {
        QVariantMap m;
        m["timestamp"] = QVariant::fromValue<qint64>(bucketStart);
        m["open"] = o; m["high"] = h; m["low"] = l;
        m["close"] = c; m["volume"] = v;
        return m;
    }
};

void MarketDataBridge::syncLiveData() {
    if (!m_model || m_symbol.isEmpty()) return;
    if (m_period != TimeShare && (m_period < Min1 || m_period > Min120)) return;

    auto& liveData = domain::market::MarketDataService::instance()
        .liveData(m_symbol.toStdString());
    if (!liveData.valid()) return;

    const auto& src = liveData.period(1).all();
    if (src.empty()) return;

    int srcCount = static_cast<int>(src.size());
    qint64 bucketMs = periodMs(m_period);
    qint64 latestTime = src.back().timeBegin();

    // ── 按目标周期聚合 1min Bar → AggBucket 序列 ──
    std::vector<AggBucket> buckets;

    for (int i = 0; i < srcCount; ) {
        qint64 bucketStart = (src[static_cast<size_t>(i)].timeBegin() / bucketMs) * bucketMs;
        AggBucket ab;
        ab.bucketStart = bucketStart;
        ab.l = 1e18;
        bool first = true;
        while (i < srcCount && src[static_cast<size_t>(i)].timeBegin() < bucketStart + bucketMs) {
            const auto& b = src[static_cast<size_t>(i)];
            if (first) { ab.o = b.open(); first = false; }
            if (b.high() > ab.h) ab.h = b.high();
            if (b.low()  < ab.l) ab.l = b.low();
            ab.c = b.close();
            ab.v += b.volume();
            ++i;
        }
        if (!first) buckets.push_back(ab);
    }

    if (buckets.empty()) return;

    int bucketCount = static_cast<int>(buckets.size());
    const auto& lastBucket = buckets.back();
    bool lastIsCurrent = (lastBucket.bucketStart + bucketMs > latestTime);

    // ── 交易日切换检测 ──
    if (!m_isFirstSync && m_modelCount > 0 && m_lastBucketKey > 0
        && (buckets.front().bucketStart - m_lastBucketKey) > 4 * 3600 * 1000) {
        m_model->clear();
        m_isFirstSync = true;
        m_modelCount = 0;
        m_lastBucketKey = -1;
    }

    // ── 首次同步: 全量加载 ──
    if (m_isFirstSync) {
        QVariantList result;
        int loadCount = lastIsCurrent ? bucketCount - 1 : bucketCount;
        if (loadCount <= 0 && !buckets.empty()) loadCount = 1;
        for (int i = 0; i < loadCount; ++i)
            result.append(buckets[static_cast<size_t>(i)].toMap());
        m_model->setCandles(result);
        m_modelCount = result.size();
        m_isFirstSync = false;
        if (lastIsCurrent) {
            m_lastBucketKey = lastBucket.bucketStart;
            m_lastClose = lastBucket.c;
            m_lastVolume = lastBucket.v;
            m_lastHigh = lastBucket.h;
            m_lastLow = lastBucket.l;
        }
        return;
    }

    // ── 增量路径 ──
    int completeCount = lastIsCurrent ? bucketCount - 1 : bucketCount;

    // 1) 追加已完成的新桶
    for (int i = m_modelCount; i < completeCount && i < bucketCount; ++i) {
        const auto& ab = buckets[static_cast<size_t>(i)];
        m_model->appendCandle(ab.bucketStart, ab.o, ab.h, ab.l, ab.c, ab.v);
        m_modelCount++;
    }

    // 2) 更新当前桶
    if (lastIsCurrent) {
        if (lastBucket.bucketStart != m_lastBucketKey) {
            m_model->appendCandle(lastBucket.bucketStart, lastBucket.o,
                                  lastBucket.h, lastBucket.l, lastBucket.c, lastBucket.v);
            m_modelCount++;
            m_lastBucketKey = lastBucket.bucketStart;
            m_lastClose = lastBucket.c;
            m_lastVolume = lastBucket.v;
            m_lastHigh = lastBucket.h;
            m_lastLow = lastBucket.l;
        } else if (lastBucket.c != m_lastClose || lastBucket.v != m_lastVolume) {
            double volDelta = lastBucket.v - m_lastVolume;
            m_model->updateLastCandle(lastBucket.c, lastBucket.h,
                                      lastBucket.l, volDelta > 0 ? volDelta : 0.0);
            m_lastClose = lastBucket.c;
            m_lastVolume = lastBucket.v;
            m_lastHigh = lastBucket.h;
            m_lastLow = lastBucket.l;
            emit tickReceived(m_symbol, lastBucket.c, lastBucket.v);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
// 热门板块数据
// ═══════════════════════════════════════════════════════════════════

void MarketDataBridge::fetchSectorHeat() {
    QVariantList result;

    char todayStr[32];
    {
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        struct tm local;
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&local, &tt);
#else
        localtime_r(&tt, &local);
#endif
        snprintf(todayStr, sizeof(todayStr), "%04d-%02d-%02d",
                 local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    }

    // 1. 获取申万一级行业 → 成分股
    auto* cats = ::stk_get_industry_category("sw", 1);
    if (!cats || cats->status() || cats->count() <= 0) {
        INTERNAL_WARN_STREAM << "[MktBridge] stk_get_industry_category failed";
        if (cats) cats->release();
        m_sectorHeatData = result; emit sectorHeatDataChanged(); return;
    }

    struct StockInfo { std::string sym; double chg; };
    struct SecData {
        std::string name;
        double chg = 0.0, netIn = 0.0, netInRate = 0.0;
        std::vector<StockInfo> leads;
        int stockCnt = 0;
    };
    std::map<std::string, SecData> secMap;
    std::vector<std::string> allSyms;
    std::vector<std::string> symIndustry;

    int catCount = std::min(cats->count(), 25);
    for (size_t ci = 0; ci < static_cast<size_t>(catCount); ++ci) {
        auto& cat = cats->at(ci);
        std::string indCode(cat.industry_code), indName(cat.industry_name);
        secMap[indName] = SecData{indName};

        auto* stocks = ::stk_get_industry_constituents(indCode.c_str(), nullptr);
        if (!stocks || stocks->status() || stocks->count() <= 0) {
            if (stocks) stocks->release(); continue;
        }
        int pick = std::min(stocks->count(), 5);
        for (int si = 0; si < pick; ++si) {
            std::string sym(stocks->at(si).symbol);
            allSyms.push_back(sym); symIndustry.push_back(indName);
        }
        stocks->release();
    }
    cats->release();
    if (allSyms.empty()) { m_sectorHeatData = result; emit sectorHeatDataChanged(); return; }

    std::string symList;
    for (size_t i = 0; i < allSyms.size(); ++i) { if (i>0) symList+=","; symList+=allSyms[i]; }
    auto* bars = ::history_bars_n(symList.c_str(), "1d", 1, todayStr, 0, nullptr, true, nullptr);
    if (bars && !bars->status() && bars->count() > 0) {
        for (size_t i = 0; i < bars->count() && i < allSyms.size(); ++i) {
            auto& b = bars->at(i);
            if (b.close <= 0 || b.pre_close <= 0) continue;
            double chg = (b.close - b.pre_close) / b.pre_close * 100.0;
            auto& sd = secMap[symIndustry[i]];
            sd.chg += chg; sd.stockCnt++;
            sd.leads.push_back({allSyms[i], chg});
        }
        bars->release();
    } else { if (bars) bars->release(); }

    auto* mf = ::stk_get_money_flow(symList.c_str(), nullptr);
    if (mf && !mf->status() && mf->count() > 0) {
        for (size_t i = 0; i < mf->count(); ++i) {
            auto& r = mf->at(i);
            for (size_t j = 0; j < allSyms.size(); ++j) {
                if (allSyms[j] == std::string(r.symbol)) {
                    auto& sd = secMap[symIndustry[j]];
                    sd.netIn += r.main_net_in;
                    sd.netInRate += r.main_net_in_rate;
                    break;
                }
            }
        }
        mf->release();
    } else { if (mf) mf->release(); }

    struct ResultItem { std::string name; double chg, netIn, netInRate; int signal; QVariantList leads; };
    std::vector<ResultItem> items;
    for (auto& [name, sd] : secMap) {
        if (sd.stockCnt <= 0) continue;
        double avgChg = sd.chg / sd.stockCnt;
        int signal; if (avgChg>0&&sd.netIn>0) signal=0; else if (avgChg>0&&sd.netIn<0) signal=1; else if (avgChg<0&&sd.netIn>0) signal=2; else signal=3;
        std::sort(sd.leads.begin(), sd.leads.end(), [](auto& a, auto& b){ return std::abs(a.chg) > std::abs(b.chg); });
        QVariantList leadsList;
        for (auto& l : sd.leads) { QVariantMap lm; lm["sym"]=QString::fromStdString(l.sym); lm["c"]=l.chg; leadsList.append(lm); }
        items.push_back({name, avgChg, sd.netIn, sd.stockCnt>0?sd.netInRate/sd.stockCnt:0, signal, leadsList});
    }
    std::sort(items.begin(), items.end(), [](auto& a, auto& b){ if(a.signal!=b.signal) return a.signal<b.signal; return std::abs(a.chg)>std::abs(b.chg); });

    for (auto& it : items) {
        QVariantMap m;
        m["name"]=QString::fromStdString(it.name); m["chg"]=it.chg; m["netIn"]=it.netIn; m["netInRate"]=it.netInRate;
        m["signal"]=it.signal; m["leads"]=it.leads;
        result.append(m);
    }

    m_sectorHeatData = result;
    emit sectorHeatDataChanged();
    INTERNAL_INFO_STREAM << "[MktBridge] fetchSectorHeat done sectors=" << result.size()
                         << " stocks=" << allSyms.size();
}

bool MarketDataBridge::forceSyncToday() {
    static astock::infrastructure::database::PostMarketSyncService s;
    return s.forceSyncToday();
}

} // namespace bridge
