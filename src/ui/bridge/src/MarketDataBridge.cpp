#include "MarketDataBridge.h"
#include "../../engine/include/GmSessionEngine.h"
#include "../../../domain/trading/include/MarketDataUtils.h"
#include "../../../infrastructure/include/database/MarketDataRepository.h"
#include "../../../infrastructure/include/database/NativeMySQLConnectionPool.h"
#include "foundation/market/AStockSymbol.h"

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

    // 行情数据由 ensureWatchSymbol → updateSnapshot() 触发 fetchQuote() 拉取

    emit initializedChanged();
    emit connectedChanged();
}

void MarketDataBridge::initializeAsync() {
    initialize();
}

void MarketDataBridge::updateSnapshot(const QString& symbol) {
    if (symbol.isEmpty()) return;
    // 纯数字码补后缀: "000001" → "000001.SZ"
    std::string sym = symbol.toStdString();
    if (sym.find('.') == std::string::npos && sym.size() == 6) {
        auto symObj = foundation::market::AStockSymbol::fromCode(sym);
        if (symObj.isValid()) sym = symObj.fullSymbol();
    }
    auto q = engine::GmSessionEngine::instance().fetchQuote(sym);
    if (!q || !q->valid) {
        INTERNAL_WARN_STREAM << "[MktBridge] updateSnapshot FAILED for " << symbol.toStdString();
        return;
    }
    INTERNAL_INFO_STREAM << "[MktBridge] updateSnapshot OK " << symbol.toStdString()
                         << " price=" << q->price << " source=" << (q->preClose == q->price ? "preClose" : "tick");

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
    m_marketSnapshots = QVariantMap(m_marketSnapshots);  // 构造新对象, 强制打破隐式共享
    emit marketSnapshotsChanged();
}

void MarketDataBridge::ensureWatchSymbol(const QString& symbol) {
    if (symbol.isEmpty()) return;

    // 补交易所后缀: "000001" → "000001.SZ" (GmSessionEngine 需要全格式)
    QString resolved = symbol;
    std::string sym = symbol.toStdString();
    if (sym.find('.') == std::string::npos && sym.size() == 6) {
        auto symObj = foundation::market::AStockSymbol::fromCode(sym);
        if (symObj.isValid()) resolved = QString::fromStdString(symObj.fullSymbol());
    }

    m_trackedSymbols.insert(resolved);
    if (m_primarySymbol != resolved) {
        m_primarySymbol = resolved;
        emit primarySymbolChanged();
        INTERNAL_INFO_STREAM << "[MktBridge] primarySymbol changed to " << resolved.toStdString();
    }
    updateSnapshot(resolved);
    engine::GmSessionEngine::instance().subscribeTick(resolved.toStdString());
}

void MarketDataBridge::activateDefaultWatchlist() {
    if (m_watchlist.isEmpty())
        m_watchlist = QStringList{"000001.SZ", "600000.SH", "600519.SH", "000858.SZ", "601318.SH", "000333.SZ"};
    m_primarySymbol = m_watchlist.first();
    for (const auto& sym : m_watchlist) ensureWatchSymbol(sym);
    emit primarySymbolChanged();
}

QVariantMap MarketDataBridge::resolveInstrument(const QString& symbol) const {
    // 补交易所后缀
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
    engine::GmSessionEngine::instance().subscribeTick(resolved.toStdString());

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
        // 补交易所后缀
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

// ── Domain 工具方法 (薄转发到 MarketDataUtils.h) ──

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

} // namespace bridge
