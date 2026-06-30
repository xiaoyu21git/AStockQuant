// StockDataLoader.cpp — K线数据加载器
// v2: 实时同步改为增量更新 (appendCandle + updateLastCandle)
#include "StockDataLoader.h"
#include "../../../domain/market/include/MarketDataService.h"
#include "../../../domain/market/include/LiveData.h"
#include "../../engine/include/GmSessionEngine.h"
#include "../../../thirdparty/gmsdk/strategy.h"
#include "foundation/market/AStockSymbol.h"
#include "foundation/log/logging.hpp"

#include <QDateTime>
#include <map>
#include <vector>

namespace bridge {

StockDataLoader::StockDataLoader(QObject* parent) : QObject(parent) {
    m_timer.setInterval(500);
    connect(&m_timer, &QTimer::timeout, this, &StockDataLoader::syncLiveData);
}

StockDataLoader::~StockDataLoader() {
    m_timer.stop();
}

void StockDataLoader::resetSyncState() {
    m_isFirstSync = true;
    m_modelCount = 0;
    m_lastBucketKey = -1;
    m_lastClose = 0.0;
    m_lastVolume = 0.0;
    m_lastHigh = 0.0;
    m_lastLow = 0.0;
}

void StockDataLoader::setSymbol(const QString& s) {
    if (m_symbol != s) {
        m_symbol = s;
        if (m_model) m_model->clear();
        resetSyncState();
        emit symbolChanged();
    }
}

void StockDataLoader::setPeriod(int p) {
    if (m_period != p) {
        m_period = p;
        resetSyncState();
        emit periodChanged();
    }
}

// ── 工具: Bar → QVariantMap ──
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

// ── 聚合: 日线 → 周线 ──
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

void StockDataLoader::loadHistory(const QString& code, int period) {
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

void StockDataLoader::loadFromDB(const QString& code, int period) {
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
        if (!m_timer.isActive()) m_timer.start();
        auto daily = loadDailyBars(gmSym, 5);
        double pc = daily.isEmpty() ? 0.0 : daily.last().toMap()["close"].toDouble();
        if (pc > 0) {
            QVariantMap m;
            m["timestamp"] = QVariant::fromValue<qint64>(QDateTime::currentDateTime().toMSecsSinceEpoch());
            m["open"]=pc; m["high"]=pc; m["low"]=pc; m["close"]=pc; m["volume"]=0;
            result.append(m);
        }
    } else {
        m_timer.stop();
        auto daily = loadDailyBars(gmSym, 500);
        if (daily.isEmpty()) {
            INTERNAL_WARN_STREAM << "[StockDataLoader] daily bars empty: " << gmSym;
            return;
        }
        switch (period) {
            case Weekly:  result = aggregateWeekly(daily);  break;
            case Monthly: result = aggregateMonthly(daily); break;
            default:      result = std::move(daily);        break;
        }
    }

    m_model->setCandles(result);
    m_model->setPreClose(result.isEmpty() ? 0.0 : result.last().toMap()["close"].toDouble());
    m_modelCount = result.size();
    m_isFirstSync = false;
    emit dataReady();

    if (period == TimeShare || (period >= Min1 && period <= Min120)) {
        m_timer.start();
    }

    INTERNAL_INFO_STREAM << "[StockDataLoader] loaded " << result.size()
                         << " bars period=" << period << " gm=" << gmSym;
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
        default:     return 60'000;
    }
}

// ── 聚合桶结构 (syncLiveData 用) ──
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

// ── 实时同步: 增量更新 ──
void StockDataLoader::syncLiveData()
{
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

    // ── 首次同步: 全量加载 ──
    if (m_isFirstSync) {
        QVariantList result;
        int loadCount = lastIsCurrent ? bucketCount - 1 : bucketCount;
        if (loadCount <= 0 && !buckets.empty()) loadCount = 1; // 至少加载当前桶
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
            // 新桶: 追加
            m_model->appendCandle(lastBucket.bucketStart, lastBucket.o,
                                  lastBucket.h, lastBucket.l, lastBucket.c, lastBucket.v);
            m_modelCount++;
            m_lastBucketKey = lastBucket.bucketStart;
            m_lastClose = lastBucket.c;
            m_lastVolume = lastBucket.v;
            m_lastHigh = lastBucket.h;
            m_lastLow = lastBucket.l;
        } else if (lastBucket.c != m_lastClose || lastBucket.v != m_lastVolume) {
            // 同桶更新
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

} // namespace bridge
