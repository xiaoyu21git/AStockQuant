// MarketDataBridge.cpp — 统一行情桥接层
// tick 事件驱动 (被动推送), 同时处理行情快照 + K线聚合, 零定时器
#include "MarketDataBridge.h"
#include "../../../domain/market/include/MarketDataService.h"
#include "../../../domain/market/include/LiveData.h"
#include "../../engine/include/GmSessionEngine.h"
#include "../../../domain/trading/include/MarketDataUtils.h"
#include "../../../infrastructure/include/database/MarketDataRepository.h"
#include "../../../infrastructure/include/database/NativePgConnectionPool.h"
#include "../../engine/include/GlobalEventBusRegistry.h"
#include "../../../infrastructure/include/database/PostMarketSyncService.h"
#include "../../../thirdparty/gmsdk/strategy.h"
#include "foundation/market/AStockSymbol.h"

#include <QDateTime>
#include <QDate>
#include <map>
#include <unordered_map>
#include <vector>
#include <cmath>
#include "foundation/log/logging.hpp"

namespace bridge {

MarketDataBridge::MarketDataBridge(QObject* parent) : QObject(parent) {}

MarketDataBridge::~MarketDataBridge() {
    stopSectorHeatThread();
    if (m_tickSub.is_valid()) {
        auto bus = engine::get_engine_event_bus();
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
    auto bus = engine::get_engine_event_bus();
    if (bus && bus->is_running()) {
        m_tickSub = bus->subscribe("trading.market.tick",
            [this](const engine::EventFormat& evt) { onTickEvent(evt); });
    }

    emit initializedChanged();
    emit connectedChanged();

    // 启动板块热度后台拉取线程
    startSectorHeatThread();
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

// ── 板块热度后台线程 ──
void MarketDataBridge::startSectorHeatThread() {
    m_sectorThreadRunning = true;
    m_sectorThread = std::thread([this]() {
        // 首次拉取: 在工作线程直接调 gm SDK, 不阻塞主线程
        {
            QVariantList result;
            fetchSectorHeatInternal(result);
            QMetaObject::invokeMethod(this, [this, result]() {
                m_sectorHeatData = result;
                emit sectorHeatDataChanged();
            }, Qt::QueuedConnection);
        }
        while (m_sectorThreadRunning) {
            // 等待间隔, 但 m_sectorFetchRequested 可打断 (QML 手动刷新)
            for (int i = 0; i < kSectorHeatIntervalSec && m_sectorThreadRunning; ++i) {
                if (m_sectorFetchRequested.load()) break;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (!m_sectorThreadRunning) break;
            m_sectorFetchRequested = false;

            QVariantList result;
            fetchSectorHeatInternal(result);  // 工作线程跑 gm SDK
            QMetaObject::invokeMethod(this, [this, result]() {
                m_sectorHeatData = result;
                emit sectorHeatDataChanged();
            }, Qt::QueuedConnection);
        }
    });
}

void MarketDataBridge::stopSectorHeatThread() {
    m_sectorThreadRunning = false;
    if (m_sectorThread.joinable())
        m_sectorThread.join();
}

// ═══════════════════════════════════════════════════════════════════
// 行情快照
// ═══════════════════════════════════════════════════════════════════

void MarketDataBridge::updateSnapshot(const QString& symbol) {
    if (symbol.isEmpty()) return;
    std::string sym = symbol.toStdString();
    sym = foundation::market::AStockSymbol::normalizeToFullSymbol(sym);

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
    snap["limitUpPrice"]  = d.limitUp();
    snap["limitDownPrice"]= d.limitDown();
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

    QString resolved = QString::fromStdString(
        foundation::market::AStockSymbol::normalizeToFullSymbol(symbol.toStdString()));

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
    QString resolved = QString::fromStdString(
        foundation::market::AStockSymbol::normalizeToFullSymbol(symbol.toStdString()));

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

    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
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
        QString resolved = QString::fromStdString(
            foundation::market::AStockSymbol::normalizeToFullSymbol(s.toStdString()));
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
    sym = foundation::market::AStockSymbol::normalizeToFullSymbol(sym);
    m_symbol = QString::fromStdString(sym);
    m_period = period;
    resetSyncState();
    loadFromDB(code, period);
}

void MarketDataBridge::loadFromDB(const QString& code, int period) {
    if (!m_model) return;
    std::string sym = code.toStdString();
    sym = foundation::market::AStockSymbol::normalizeToFullSymbol(sym);
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

// ── gm SDK 拉取 (在调用线程执行) ──
// 方案B: 直接查交易所行业指数 K 线获取板块涨跌，成分股只用于领涨名单
void MarketDataBridge::fetchSectorHeatInternal(QVariantList& result) {
    result.clear();

    // ── 交易所行业指数列表 (SHSE + SZSE) ──
    struct SectorDef { const char* sym; const char* name; };
    static const SectorDef kSectors[] = {
        // 上证行业指数 (10个)
        {"SHSE.000032", "能源"},
        {"SHSE.000033", "材料"},
        {"SHSE.000034", "工业"},
        {"SHSE.000035", "可选消费"},
        {"SHSE.000036", "主要消费"},
        {"SHSE.000037", "医药卫生"},
        {"SHSE.000038", "金融地产"},
        {"SHSE.000039", "信息技术"},
        {"SHSE.000040", "电信服务"},
        {"SHSE.000041", "公用事业"},
        // 深证行业指数 (16个)
        {"SZSE.399231", "深证农林"},
        {"SZSE.399232", "深证采矿"},
        {"SZSE.399233", "深证制造"},
        {"SZSE.399234", "深证水电"},
        {"SZSE.399235", "深证建筑"},
        {"SZSE.399236", "深证批零"},
        {"SZSE.399237", "深证运输"},
        {"SZSE.399238", "深证餐饮"},
        {"SZSE.399239", "深证IT"},
        {"SZSE.399240", "深证金融"},
        {"SZSE.399241", "深证地产"},
        {"SZSE.399242", "深证商务"},
        {"SZSE.399243", "深证科研"},
        {"SZSE.399244", "深证公共"},
        {"SZSE.399248", "深证文化"},
        {"SZSE.399249", "深证综企"},
    };
    constexpr int kSectorCount = sizeof(kSectors) / sizeof(kSectors[0]);

    struct StockInfo { std::string sym; std::string name; double chg; };
    struct SecData {
        double chg = 0, netIn = 0, netInRate = 0;
        double indexAmount = 0;   // 实时成交额 (盘中用, 盘后被 money_flow 覆盖)
        bool hasMoneyFlow = false; // true=netIn 来自 money_flow
        std::vector<StockInfo> leads;
        int stockCnt = 0;
    };
    std::unordered_map<std::string, SecData> secMap;  // sectorName → data
    std::vector<std::string> allSyms, symSector, allNames;  // 成分股聚合用

    // ── ① 指数 K 线: 板块涨跌 + 实时成交额 ──
    int idxOk = 0, idxRealtime = 0;
    for (int i = 0; i < kSectorCount; ++i) {
        double yesterdayClose = 0, todayPrice = 0, todayAmount = 0;

        auto* dailyBars = ::history_bars_n(kSectors[i].sym, "1d", 1, nullptr, 0, nullptr, true, nullptr);
        if (dailyBars && !dailyBars->status() && dailyBars->count() > 0)
            yesterdayClose = dailyBars->at(0).close;
        if (dailyBars) dailyBars->release();

        auto* bar60s = ::history_bars_n(kSectors[i].sym, "60s", 1, nullptr, 0, nullptr, true, nullptr);
        if (bar60s && !bar60s->status() && bar60s->count() > 0 && bar60s->at(0).close > 0) {
            todayPrice = bar60s->at(0).close;
            todayAmount = bar60s->at(0).amount;  // 实时成交额
            idxRealtime++;
        }
        if (bar60s) bar60s->release();

        double price = todayPrice > 0 ? todayPrice : yesterdayClose;
        if (price > 0 && yesterdayClose > 0) {
            double chg = (price - yesterdayClose) / yesterdayClose * 100.0;
            secMap[kSectors[i].name].chg = chg;
            secMap[kSectors[i].name].indexAmount = todayAmount;
            secMap[kSectors[i].name].stockCnt = 1;
            idxOk++;
        } else {
            secMap[kSectors[i].name] = SecData{};
        }
    }
    INTERNAL_INFO_STREAM << "[MktBridge] ① index K-line done: " << idxOk << "/" << kSectorCount
                         << " realtime60s=" << idxRealtime;

    // ── ② 成分股: 领涨股榜单 ──
    for (int i = 0; i < kSectorCount; ++i) {
        auto* stocks = ::stk_get_index_constituents(kSectors[i].sym, nullptr);
        if (!stocks || stocks->status() || stocks->count() <= 0) {
            if (stocks) stocks->release();
            continue;
        }
        int pick = std::min(stocks->count(), 5);
        for (int si = 0; si < pick; ++si) {
            allSyms.push_back(std::string(stocks->at(si).symbol));
            symSector.push_back(kSectors[i].name);
        }
        stocks->release();
    }
    INTERNAL_INFO_STREAM << "[MktBridge] ② constituents collected: " << allSyms.size() << " stocks";

    // 成分股名称批量查询 (StkIndexConstituent 无 sec_name, 用 stk_get_symbol_industry 查)
    std::unordered_map<std::string, std::string> symToName;
    if (!allSyms.empty()) {
        std::string symList;
        for (size_t i = 0; i < allSyms.size(); ++i) {
            if (i > 0) symList += ",";
            symList += allSyms[i];
        }
        auto* si = ::stk_get_symbol_industry(symList.c_str(), "sw2021", 1, nullptr);
        if (si && !si->status() && si->count() > 0) {
            for (size_t i = 0; i < si->count(); ++i)
                symToName[si->at(i).symbol] = si->at(i).sec_name;
            si->release();
        } else if (si) {
            si->release();
        }
        if (symToName.empty()) {
            // fallback: 用 symbol 本身作为 name
            for (const auto& s : allSyms) symToName[s] = s;
        }
    }

    bool hasConstituents = !allSyms.empty();
    std::string tradeDate;
    if (hasConstituents) {
        // symbol→sector 映射 (资金流向反查用)
        std::unordered_map<std::string, std::string> symToSector;
        for (size_t i = 0; i < allSyms.size(); ++i)
            symToSector[allSyms[i]] = symSector[i];

        // ── ③ 成分股 60s+日线: 领涨股涨跌幅 ──
        int barOk = 0, realtimeCount = 0;
        for (size_t i = 0; i < allSyms.size(); ++i) {
            const auto& sym = allSyms[i];
            double yesterdayClose = 0, todayPrice = 0;

            auto* dailyBars = ::history_bars_n(sym.c_str(), "1d", 1, nullptr, 0, nullptr, true, nullptr);
            if (dailyBars && !dailyBars->status() && dailyBars->count() > 0)
                yesterdayClose = dailyBars->at(0).close;
            if (dailyBars) dailyBars->release();

            auto* bar60s = ::history_bars_n(sym.c_str(), "60s", 1, nullptr, 0, nullptr, true, nullptr);
            if (bar60s && !bar60s->status() && bar60s->count() > 0 && bar60s->at(0).close > 0) {
                todayPrice = bar60s->at(0).close;
                realtimeCount++;
            }
            if (bar60s) bar60s->release();

            double price = todayPrice > 0 ? todayPrice : yesterdayClose;
            if (price > 0 && yesterdayClose > 0) {
                double chg = (price - yesterdayClose) / yesterdayClose * 100.0;
                auto& sd = secMap[symSector[i]];
                sd.leads.emplace_back(StockInfo{sym, symToName[sym], chg});
                barOk++;
            }
            // 若指数K线没拿到涨跌，用成分股均值兜底
            auto it = secMap.find(symSector[i]);
            if (it != secMap.end() && it->second.stockCnt == 0 && price > 0 && yesterdayClose > 0) {
                double chg = (price - yesterdayClose) / yesterdayClose * 100.0;
                it->second.chg += chg;
                it->second.stockCnt++;
            }
        }
        INTERNAL_INFO_STREAM << "[MktBridge] ③ stock bars: " << barOk << "/" << allSyms.size()
                             << " realtime60s=" << realtimeCount;

        // ── ④ 资金流向 (仅盘后可用, 盘中用指数成交额替代) ──
        std::string symList2;
        for (size_t i = 0; i < allSyms.size(); ++i) { if (i > 0) symList2 += ","; symList2 += allSyms[i]; }
        auto* mf = ::stk_get_money_flow(symList2.c_str(), nullptr);
        if (mf && !mf->status() && mf->count() > 0) {
            for (size_t i = 0; i < mf->count(); ++i) {
                auto& r = mf->at(i);
                if (tradeDate.empty() && r.trade_date[0]) tradeDate = r.trade_date;
                auto it = symToSector.find(std::string(r.symbol));
                if (it == symToSector.end()) continue;
                auto& sd = secMap[it->second];
                sd.netIn += r.main_net_in;
                sd.netInRate += r.main_net_in_rate;
                sd.hasMoneyFlow = true;
            }
            INTERNAL_INFO_STREAM << "[MktBridge] ④ money_flow OK records=" << mf->count() << " tradeDate=" << tradeDate;
            mf->release();
        } else {
            INTERNAL_INFO_STREAM << "[MktBridge] ④ money_flow N/A (盘中, 用指数成交额替代)"
                                 << (mf ? " status=" + std::to_string(mf->status()) : "");
            if (mf) mf->release();
        }
    }

    // ── 盘中兜底: 无 money_flow 数据的板块用指数实时成交额 ──
    for (auto& [name, sd] : secMap) {
        if (!sd.hasMoneyFlow && sd.indexAmount > 0) {
            sd.netIn = sd.indexAmount;
            sd.netInRate = 0;
        }
    }

    // ── ⑤ 资金趋势 + 排序 + 组装结果 ──
    auto computeTrend = [&](const std::string& name, double todayNetIn) -> int {
        auto it = m_sectorNetInHistory.find(name);
        if (it == m_sectorNetInHistory.end() || it->second.empty())
            return (todayNetIn > 0 ? 1 : -1);
        bool todayPositive = todayNetIn > 0;
        int consecutive = 1;
        const auto& hist = it->second;
        for (auto rit = hist.rbegin(); rit != hist.rend(); ++rit) {
            if ((*rit > 0) == todayPositive) consecutive++;
            else break;
        }
        return todayPositive ? consecutive : -consecutive;
    };
    auto getPrevNetIn = [&](const std::string& name) -> double {
        auto it = m_sectorNetInHistory.find(name);
        if (it != m_sectorNetInHistory.end() && !it->second.empty())
            return it->second.back();
        return 0.0;
    };

    struct ResultItem { std::string name; double chg, netIn, netInRate; int signal, trend; double prevNetIn; QVariantList leads; };
    std::vector<ResultItem> items;
    for (auto& [name, sd] : secMap) {
        double sectorChg = (sd.stockCnt > 0) ? sd.chg / sd.stockCnt : 0;
        int signal;
        if (sectorChg > 0 && sd.netIn > 0) signal = 0;
        else if (sectorChg > 0 && sd.netIn < 0) signal = 1;
        else if (sectorChg < 0 && sd.netIn > 0) signal = 2;
        else signal = 3;
        int trend = computeTrend(name, sd.netIn);
        double prevNetIn = getPrevNetIn(name);

        std::sort(sd.leads.begin(), sd.leads.end(),
                  [](auto& a, auto& b) { return std::abs(a.chg) > std::abs(b.chg); });
        QVariantList leadsList;
        for (auto& l : sd.leads) {
            QVariantMap lm;
            lm["sym"] = QString::fromStdString(l.sym);
            lm["name"] = QString::fromStdString(l.name);
            lm["c"] = l.chg;
            leadsList.append(lm);
        }
        items.push_back({name, sectorChg, sd.netIn, sd.stockCnt > 0 ? sd.netInRate / sd.stockCnt : 0,
                         signal, trend, prevNetIn, leadsList});
    }
    std::sort(items.begin(), items.end(),
              [](auto& a, auto& b) { if (a.signal != b.signal) return a.signal < b.signal; return std::abs(a.chg) > std::abs(b.chg); });

    for (auto& it : items) {
        QVariantMap m;
        m["name"] = QString::fromStdString(it.name);
        m["chg"] = it.chg;
        m["netIn"] = it.netIn;
        m["netInRate"] = it.netInRate;
        m["signal"] = it.signal;
        m["leads"] = it.leads;
        m["netInTrend"] = it.trend;
        m["netInPrev"] = it.prevNetIn;
        result.append(m);
    }

    // 更新历史缓存
    if (!tradeDate.empty()) {
        if (m_sectorHeatDates.empty() || m_sectorHeatDates.back() != tradeDate) {
            while (static_cast<int>(m_sectorHeatDates.size()) >= kSectorHeatHistoryDays) {
                m_sectorHeatDates.pop_front();
                for (auto& kv : m_sectorNetInHistory) {
                    if (!kv.second.empty()) kv.second.pop_front();
                }
            }
            m_sectorHeatDates.push_back(tradeDate);
            for (auto& [name, sd] : secMap)
                m_sectorNetInHistory[name].push_back(sd.netIn);
        } else {
            for (auto& [name, sd] : secMap) {
                auto& hist = m_sectorNetInHistory[name];
                if (!hist.empty()) hist.back() = sd.netIn;
            }
        }
    }

    INTERNAL_INFO_STREAM << "[MktBridge] ⑤ done sectors=" << result.size()
                         << " stocks=" << allSyms.size();
}

// ── Q_INVOKABLE: QML 调用, 立即返回不阻塞 ──
// 设置请求标志 → 工作线程被打断 → 拉取 gm SDK → invokeMethod 回主线程 emit
void MarketDataBridge::fetchSectorHeat() {
    m_sectorFetchRequested = true;
}

QString MarketDataBridge::forceSyncToday() {
    auto& s = astock::infrastructure::database::PostMarketSyncService::instance();
    if (s.forceSyncToday())
        return QStringLiteral("同步已启动, 查看日志");
    return QStringLiteral("同步失败或已在运行中");
}

QString MarketDataBridge::forceSyncHistory() {
    auto& s = astock::infrastructure::database::PostMarketSyncService::instance();
    s.forceSyncHistory();
    return QStringLiteral("历史回补已启动 (日线+分钟线, 从2015至今), 查看日志");
}

QString MarketDataBridge::forceSyncDate(int tradingDay) {
    auto& s = astock::infrastructure::database::PostMarketSyncService::instance();
    s.forceSyncDate(tradingDay);
    return QStringLiteral("补同步已启动: %1").arg(tradingDay);
}

QString MarketDataBridge::forceSyncMissingDays(int lookbackDays) {
    auto& s = astock::infrastructure::database::PostMarketSyncService::instance();
    s.forceSyncMissingDays(lookbackDays);
    return QStringLiteral("缺口补齐已启动, 回溯%1天, 查看日志").arg(lookbackDays);
}

QString MarketDataBridge::probeGmCoverage(const QString& symbol, const QVariantList& dates) {
    std::vector<std::string> dts;
    for (const auto& d : dates) dts.push_back(d.toString().toStdString());
    astock::infrastructure::database::PostMarketSyncService::instance().probeGmCoverage(
        symbol.toStdString(), dts);
    return QStringLiteral("探测已启动, 查看日志");
}

QString MarketDataBridge::fillAdjFactors() {
    astock::infrastructure::database::PostMarketSyncService::instance().fillAdjFactors();
    return QStringLiteral("复权因子补全已启动, 查看日志");
}

} // namespace bridge
