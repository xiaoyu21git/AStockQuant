// StockDataLoader.cpp — K线数据加载器 (日线+聚合+分时+实时更新)
#include "StockDataLoader.h"
#include "../../engine/include/GmSessionEngine.h"
#include "../../engine/include/Event/EventBus.hpp"
#include "../../engine/include/Event/EventFormat.hpp"
#include "../../engine/include/GlobalEventBusRegistry.h"
#include "../../../thirdparty/gmsdk/strategy.h"
#include "foundation/market/AStockSymbol.h"
#include "foundation/log/logging.hpp"

#include <QDateTime>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

namespace bridge {

StockDataLoader::StockDataLoader(QObject* parent) : QObject(parent) {
    subscribeToEventBus();
}

StockDataLoader::~StockDataLoader() {
    auto* bus = engine::get_engine_event_bus();
    if (bus && !m_tickSub.is_null()) {
        bus->unsubscribe(m_tickSub);
    }
}

void StockDataLoader::subscribeToEventBus() {
    auto* bus = engine::get_engine_event_bus();
    if (!bus) {
        INTERNAL_WARN_STREAM << "[StockDataLoader] EventBus not available, tick subscription skipped";
        return;
    }
    if (!bus->is_running()) {
        INTERNAL_WARN_STREAM << "[StockDataLoader] EventBus exists but NOT RUNNING, tick subscription skipped";
        return;
    }
    // 限流计数器 (static, 跨回调共享)
    auto tickCount = std::make_shared<std::atomic<int>>(0);
    auto lastLogTime = std::make_shared<std::atomic<qint64>>(0);

    m_tickSub = bus->subscribe("trading.market.tick",
        [this, tickCount, lastLogTime](const engine::EventFormat& e) {
            int n = tickCount->fetch_add(1) + 1;
            auto sym   = e.get<std::string>("symbol");
            auto price = e.get<double>("price");
            auto vol   = e.get<double>("volume");

            // 每 5 秒打印一次 tick 汇总
            qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            qint64 last = lastLogTime->load();
            if (nowMs - last >= 5000) {
                if (lastLogTime->compare_exchange_strong(last, nowMs)) {
                    INTERNAL_INFO_STREAM << "[StockDataLoader] EventBus tick #" << n
                        << " sym=" << (sym.has_value() ? *sym : "null")
                        << " price=" << (price.has_value() ? *price : -1.0)
                        << " m_symbol=" << m_symbol.toStdString()
                        << " hasCurrentCandle=" << m_hasCurrentCandle;
                }
            }

            if (!sym.has_value() || !price.has_value()) return;

            // 跨线程安全: 通过 QueuedConnection 将数据更新切换到 GUI 线程
            QMetaObject::invokeMethod(this, [this, s = QString::fromStdString(*sym),
                p = *price, v = vol.value_or(0.0)]() {
                qint64 now = QDateTime::currentMSecsSinceEpoch();
                onTick(s, p, v, now);
                emit tickReceived(s, p, v);
            }, Qt::QueuedConnection);
        });
    INTERNAL_INFO_STREAM << "[StockDataLoader] subscribed to trading.market.tick, bus running=" << bus->is_running();
}

void StockDataLoader::setSymbol(const QString& s) {
    if (m_symbol != s) { m_symbol = s; m_model->clear(); m_hasCurrentCandle = false; emit symbolChanged(); }
}
void StockDataLoader::setPeriod(int p) {
    if (m_period != p) { m_period = p; m_hasCurrentCandle = false; emit periodChanged(); }
}

// ── 工具: 秒时间戳 → QDateTime ──
static QDateTime fromBob(double bob) {
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(bob));
}

// ── 聚合: 日线 → 周线 ──
static QVariantList aggregateWeekly(const QVariantList& daily) {
    // 按 ISO 周分组 (year * 100 + weekNumber)
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

// ── 聚合: 日线 → 月线 ──
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

// ── 加载原始日线 ──
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

// ── 加载分时 (60秒线, 今日) ──
static QVariantList loadTimeShare(const std::string& gmSym) {
    auto now = std::chrono::system_clock::now();
    auto t_now  = std::chrono::system_clock::to_time_t(now);
    std::time_t t_today = t_now;
    std::tm local;
    localtime_s(&local, &t_today);
    local.tm_hour = 0; local.tm_min = 0; local.tm_sec = 0;
    t_today = std::mktime(&local);
    char s[32], e[32];
    std::strftime(s, sizeof(s), "%Y-%m-%d", &local);
    std::strftime(e, sizeof(e), "%Y-%m-%d %H:%M:%S", std::localtime(&t_now));

    // 尝试多种 bar size 格式 (gmsdk 不同版本格式不同)
    static const char* kBarSizes[] = {"60s", "60", "1m", nullptr};
    QVariantList list;
    for (int i = 0; kBarSizes[i]; ++i) {
        auto* bars = ::history_bars(gmSym.c_str(), kBarSizes[i], s, e, 0, nullptr, true, nullptr);
        if (bars && bars->status() == 0 && bars->count() > 0) {
            INTERNAL_INFO_STREAM << "[StockDataLoader] loadTimeShare barSize=" << kBarSizes[i]
                                 << " count=" << bars->count();
            for (size_t j = 0; j < bars->count(); ++j) {
                auto& b = bars->at(j);
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
        if (bars) bars->release();
    }
    INTERNAL_WARN_STREAM << "[StockDataLoader] loadTimeShare empty for " << gmSym;
    return list;
}

void StockDataLoader::loadHistory(const QString& code, int period) {
    if (code.isEmpty()) return;
    std::string sym = code.toStdString();
    if (sym.find('.') == std::string::npos && sym.size() == 6) {
        auto obj = foundation::market::AStockSymbol::fromCode(sym);
        if (obj.isValid()) sym = obj.fullSymbol();
    }
    m_symbol = QString::fromStdString(sym);
    m_period = period;
    loadFromDB(code, period);
}

void StockDataLoader::loadFromDB(const QString& code, int period) {
    if (!m_model) return;
    std::string sym = code.toStdString();
    if (sym.find('.') == std::string::npos && sym.size() == 6) {
        auto obj = foundation::market::AStockSymbol::fromCode(sym);
        if (obj.isValid()) sym = obj.fullSymbol();
    }
    m_symbol = QString::fromStdString(sym);
    m_period = period;
    m_hasCurrentCandle = false;

    std::string gmSym = engine::GmSessionEngine::toGmSymbol(sym);
    if (gmSym.empty()) return;

    QVariantList result;

    if (period == TimeShare) {
        // 分时折线图: 加载今日 60 秒线
        result = loadTimeShare(gmSym);
        if (result.isEmpty()) {
            auto daily = loadDailyBars(gmSym, 5);
            double pc = daily.isEmpty() ? 0.0 : daily.last().toMap()["close"].toDouble();
            if (pc > 0) {
                for (int h = 9; h <= 15; h++) {
                    auto t = QDateTime::currentDateTime(); t.setTime(QTime(h, h==9?30:0));
                    QVariantMap m; m["timestamp"]=QVariant::fromValue<qint64>(t.toMSecsSinceEpoch());
                    m["open"]=pc; m["high"]=pc; m["low"]=pc; m["close"]=pc; m["volume"]=0;
                    result.append(m);
                }
            }
        }
    } else if (period >= Min1 && period <= Min120) {
        // 分钟蜡烛图: 1/5/15/30/60/120 分钟
        // bar size 秒数: 1m=60, 5m=300, 15m=900, 30m=1800, 60m=3600, 120m=7200
        const int barSecs[] = {0, 60, 300, 900, 1800, 3600, 7200};
        auto now = std::chrono::system_clock::now();
        auto t_now = std::chrono::system_clock::to_time_t(now);
        int lookbackSeconds = (period <= Min30) ? 3600 * 4 : 3600 * 8;
        auto start = now - std::chrono::seconds(lookbackSeconds);
        auto t_start = std::chrono::system_clock::to_time_t(start);
        char s[32], e[32];
        std::strftime(s, sizeof(s), "%Y-%m-%d %H:%M:%S", std::localtime(&t_start));
        std::strftime(e, sizeof(e), "%Y-%m-%d %H:%M:%S", std::localtime(&t_now));

        // 尝试多种 bar size 格式
        int secs = barSecs[period];
        char sz1[16], sz2[16], sz3[16];
        std::snprintf(sz1, sizeof(sz1), "%d", secs);         // "60"
        std::snprintf(sz2, sizeof(sz2), "%ds", secs);        // "60s"
        std::snprintf(sz3, sizeof(sz3), "%dm", secs / 60);   // "1m"
        const char* formats[] = {sz2, sz1, sz3, nullptr};     // 优先 "60s"

        for (int fi = 0; formats[fi]; ++fi) {
            auto* bars = ::history_bars(gmSym.c_str(), formats[fi], s, e, 0, nullptr, true, nullptr);
            if (bars && bars->status() == 0 && bars->count() > 0) {
                INTERNAL_INFO_STREAM << "[StockDataLoader] minute bars barSize=" << formats[fi]
                                     << " count=" << bars->count() << " period=" << period;
                for (size_t i = 0; i < bars->count(); ++i) {
                    auto& b = bars->at(i);
                    QVariantMap item;
                    item["timestamp"] = QVariant::fromValue<qint64>(static_cast<qint64>(b.bob * 1000.0));
                    item["open"]=static_cast<double>(b.open); item["high"]=static_cast<double>(b.high);
                    item["low"]=static_cast<double>(b.low);   item["close"]=static_cast<double>(b.close);
                    item["volume"]=b.volume;
                    result.append(item);
                }
                bars->release();
                break;
            }
            if (bars) bars->release();
        }
        if (result.isEmpty()) {
            INTERNAL_WARN_STREAM << "[StockDataLoader] minute bars empty for " << gmSym << " period=" << period;
            auto daily = loadDailyBars(gmSym, 5);
            double pc = daily.isEmpty() ? 0.0 : daily.last().toMap()["close"].toDouble();
            if (pc > 0) {
                for (int h = 9; h <= 15; h++) {
                    auto t = QDateTime::currentDateTime(); t.setTime(QTime(h, h==9?30:0));
                    QVariantMap m; m["timestamp"]=QVariant::fromValue<qint64>(t.toMSecsSinceEpoch());
                    m["open"]=pc; m["high"]=pc; m["low"]=pc; m["close"]=pc; m["volume"]=0;
                    result.append(m);
                }
            }
        }
    } else {
        // 日线/周线/月线: 加载日线原始数据
        auto daily = loadDailyBars(gmSym, 500);
        if (daily.isEmpty()) {
            INTERNAL_WARN_STREAM << "[StockDataLoader] daily bars empty: " << gmSym;
            return;
        }
        switch (period) {
            case Weekly:  result = aggregateWeekly(daily);  break;
            case Monthly: result = aggregateMonthly(daily); break;
            default:      result = std::move(daily);        break; // Daily
        }
    }

    m_model->setCandles(result);
    m_model->setPreClose(result.isEmpty() ? 0.0 : result.last().toMap()["close"].toDouble());
    emit dataReady();
    INTERNAL_INFO_STREAM << "[StockDataLoader] loaded " << result.size()
                         << " bars period=" << period << " gm=" << gmSym
                         << " lastClose=" << m_model->lastPrice();
}

qint64 StockDataLoader::periodStartMs(qint64 ts, int period) {
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(ts);
    if (period <= Min120) {  // TimeShare=0, Min1=1, ... Min120=6
        dt.setTime(QTime(dt.time().hour(), dt.time().minute()));
    } else if (period == Daily) {
        dt.setTime(QTime(0, 0));
    } else if (period == Weekly) {
        dt = dt.addDays(-(dt.date().dayOfWeek() - 1)); dt.setTime(QTime(0, 0));
    } else if (period == Monthly) {
        dt.setDate(QDate(dt.date().year(), dt.date().month(), 1)); dt.setTime(QTime(0, 0));
    }
    return dt.toMSecsSinceEpoch();
}

qint64 StockDataLoader::periodMs(int period) {
    switch (period) {
        case TimeShare: return 60'000;
        case Min1:   return 60'000;
        case Min5:   return 5 * 60'000LL;
        case Min15:  return 15 * 60'000LL;
        case Min30:  return 30 * 60'000LL;
        case Min60:  return 60 * 60'000LL;
        case Min120: return 120 * 60'000LL;
        case Daily:  return 24 * 3600'000LL;
        case Weekly: return 7 * 24 * 3600'000LL;
        case Monthly:return 30 * 24 * 3600'000LL;
        default:     return 60'000;
    }
}

void StockDataLoader::onTick(const QString& symbol, double price, double volume, qint64 timestamp) {
    // ── 限流日志: 符号不匹配时每 10 次打印一次 ──
    static int mismatchCount = 0;
    if (!m_model) {
        if (++mismatchCount % 20 == 0)
            INTERNAL_WARN_STREAM << "[StockDataLoader] onTick: m_model is NULL, tick dropped x" << mismatchCount;
        return;
    }
    if (symbol != m_symbol) {
        if (++mismatchCount % 10 == 0)
            INTERNAL_WARN_STREAM << "[StockDataLoader] onTick: symbol mismatch tick="
                << symbol.toStdString() << " my=" << m_symbol.toStdString()
                << " dropped x" << mismatchCount;
        return;
    }
    mismatchCount = 0;  // reset on successful match

    qint64 pStart = periodStartMs(timestamp, m_period);

    if (!m_hasCurrentCandle || timestamp >= m_currentCandle.timestamp + periodMs(m_period)) {
        if (m_hasCurrentCandle)
            m_model->updateLastCandle(m_currentCandle.close, m_currentCandle.high, m_currentCandle.low, 0);
        m_currentCandle = {pStart, price, price, price, price, volume};
        m_model->appendCandle(pStart, price, price, price, price, volume);
        m_hasCurrentCandle = true;
        INTERNAL_INFO_STREAM << "[StockDataLoader] 🟢 new candle: " << symbol.toStdString()
                             << " price=" << price << " vol=" << volume
                             << " period=" << m_period << " count=" << m_model->rowCount();
    } else {
        m_currentCandle.high   = std::max(m_currentCandle.high, price);
        m_currentCandle.low    = std::min(m_currentCandle.low, price);
        m_currentCandle.close  = price;
        m_currentCandle.volume += volume;
        m_model->updateLastCandle(price, m_currentCandle.high, m_currentCandle.low, volume);
    }
}

} // namespace bridge
