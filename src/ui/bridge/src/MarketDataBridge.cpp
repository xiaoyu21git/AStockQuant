#include "MarketDataBridge.h"
#include "../../../domain/market/include/MarketDataService.h"
#include "../../../domain/market/include/LiveData.h"
#include "../../engine/include/GmSessionEngine.h"
#include "../../../domain/trading/include/MarketDataUtils.h"
#include "../../../infrastructure/include/database/MarketDataRepository.h"
#include "../../../infrastructure/include/database/NativeMySQLConnectionPool.h"
#include "foundation/market/AStockSymbol.h"

#include <QDateTime>
#include <QDate>
#include <cmath>
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
    std::string sym = symbol.toStdString();
    if (sym.find('.') == std::string::npos && sym.size() == 6) {
        auto symObj = foundation::market::AStockSymbol::fromCode(sym);
        if (symObj.isValid()) sym = symObj.fullSymbol();
    }

    auto& d = domain::market::MarketDataService::instance().liveData(sym);
    if (!d.valid() || d.dailyBar().close() <= 0.0) {
        INTERNAL_WARN_STREAM << "[MktBridge] updateSnapshot no data for " << sym;
        return;
    }
    const auto& bar = d.dailyBar();
    const auto& depth = d.depth();
    double pc = d.preClose();

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
    INTERNAL_INFO_STREAM << "[MktBridge] updateSnapshot OK " << sym
                         << " price=" << bar.close() << " chg=" << changePct << "%";
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

    // 订阅实时 tick — 否则 GmSessionEngine 不会向 MarketDataService 推送该标的行情
    engine::GmSessionEngine::instance().subscribeTick(resolved.toStdString());

    if (m_primarySymbol != resolved) {
        m_primarySymbol = resolved;
        emit primarySymbolChanged();
        INTERNAL_INFO_STREAM << "[MktBridge] primarySymbol changed to " << resolved.toStdString();
    }
    updateSnapshot(resolved);
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
