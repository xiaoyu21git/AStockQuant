#include "MarketDataService.h"
#include "../../../engine/include/GmSessionEngine.h"

#include <algorithm>
#include <ctime>
#include <sstream>
#include <foundation/log/logging.hpp>
#include <foundation/time/LocalClock.h>

namespace domain::market {

MarketDataService& MarketDataService::instance()
{
    static MarketDataService s_instance;
    return s_instance;
}

MarketDataService::EndOfDayCallbackToken MarketDataService::registerEndOfDayCallback(EndOfDayCallback cb)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    auto token = m_nextCallbackToken++;
    m_eodCallbacks.push_back(CallbackEntry{token, std::move(cb)});
    return token;
}

void MarketDataService::setEodCallbackWindow(int startMinute, int endMinute) {
    m_eodCallbackStartMin.store(startMinute);
    m_eodCallbackEndMin.store(endMinute);
    m_eodWindowWarned.store(false);
}

void MarketDataService::unregisterEndOfDayCallback(EndOfDayCallbackToken token)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    m_eodCallbacks.erase(std::remove_if(m_eodCallbacks.begin(), m_eodCallbacks.end(),
        [token](const CallbackEntry& e) { return e.token == token; }),
        m_eodCallbacks.end());
}

void MarketDataService::fireCallbacksForDay(std::int64_t day)
{
    if (day <= 0) return;
    // 拷贝回调列表（持有锁），释放锁后调用
    std::vector<CallbackEntry> callbacks;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (m_eodCallbacks.empty()) return;
        callbacks = m_eodCallbacks;
    }
    std::string dayStr = std::to_string(day);
    INTERNAL_INFO_STREAM << "[MarketDataService] 触发 " << callbacks.size()
                         << " EOD callbacks for day " << dayStr;
    for (auto& e : callbacks) {
        e.cb(dayStr);
    }
}

void MarketDataService::onTick(const engine::GmTickData& td)
{
    if (td.symbol.empty()) {
        INTERNAL_WARN_STREAM << "[MarketDataService] 空标的 Tick 已丢弃";
        return;
    }

    std::int64_t prevTradingDay = 0;
    bool dayChanged = false;

    {
        const std::lock_guard<std::mutex> lock(mutex_);

        // ── tradingDay 变更检测 ──
        if (td.tradingDay > 0 && m_activeTradingDay > 0
            && td.tradingDay != m_activeTradingDay) {
            prevTradingDay = m_activeTradingDay;
            dayChanged = true;
            INTERNAL_INFO_STREAM << "[MarketDataService] 交易日已变更: "
                                 << prevTradingDay << " → " << td.tradingDay;
            // 新交易日：清空所有标的旧分时 K 线，随后的代码用当前 tick 绘制首根 Bar
            for (auto& [sym, ld] : data_) {
                ld.period(1).clear();
            }
        }
        if (td.tradingDay > 0) {
            m_activeTradingDay = td.tradingDay;
        }

        auto& d = data_[td.symbol];
        if (!d.valid()) {
            d = LiveData(td.symbol);
            INTERNAL_DEBUG_STREAM << "[MarketDataService] 新标的正追踪: " << td.symbol;
        }

        // ── 更新今日日K ──
        auto& daily = d.dailyBar();
        daily.setOpen(td.open);
        daily.setHigh(td.high);
        daily.setLow(td.low);
        daily.setClose(td.price);
        daily.setVolume(td.cumVolume);
        daily.setIsAuction(td.isAuction);

        // ── 更新五档盘口 ──
        auto& depth = d.depth();
        int n = std::min(5, static_cast<int>(std::min(td.bidPrices.size(), td.askPrices.size())));
        depth.setLevelCount(n);
        for (int i = 0; i < n; ++i) {
            depth.setBidPrice(i, td.bidPrices[i]);
            depth.setBidVolume(i, i < static_cast<int>(td.bidVolumes.size()) ? td.bidVolumes[i] : 0.0);
            depth.setAskPrice(i, td.askPrices[i]);
            depth.setAskVolume(i, i < static_cast<int>(td.askVolumes.size()) ? td.askVolumes[i] : 0.0);
        }
        depth.setUpdateTime(td.createdAt * 1000);

        // ── 更新 1 分钟K ──
        const std::int64_t tsMs = td.createdAt * 1000;
        double tickAmount = td.price * td.lastVolume;

        {
            auto& s = d.period(1);
            std::int64_t minuteKey = (tsMs / 60'000) * 60'000;
            if (s.empty() || s.all().back().timeBegin() != minuteKey) {
                Bar b;
                b.setTimeBegin(minuteKey);
                b.setOpen(td.price);
                b.setHigh(td.price);
                b.setLow(td.price);
                b.setClose(td.price);
                b.setVolume(td.lastVolume);
                b.setAmount(tickAmount);
                b.setIsAuction(td.isAuction);
                s.push(std::move(b));
            } else {
                s.updateLast(td.price, td.lastVolume, tickAmount, td.isAuction);
            }
        }

        daily.setAmount(d.period(1).amountSum(d.period(1).count()));
    }
    // ── 锁释放后 → 预收盘触发 + 日切触发 ──
    {
        // tick 本地时间 (P6: 分钟换算复用 foundation::time::LocalClock, 消除 localtime 副本)
        const int minutes = foundation::time::LocalClock::minutesOfDay(
            static_cast<time_t>(td.createdAt));

        // ── 预收盘触发: 窗口由配置文件 eodCallbackStartTime/eodCallbackEndTime 决定 (零硬编码零兜底) ──
        const int cbStart = m_eodCallbackStartMin.load();
        const int cbEnd = m_eodCallbackEndMin.load();
        if (cbStart > 0 && cbEnd > 0 && cbStart < cbEnd) {
            if (minutes >= cbStart && minutes < cbEnd
                && m_activeTradingDay > 0
                && m_activeTradingDay != m_lastEvalTradingDay) {
                m_lastEvalTradingDay = m_activeTradingDay;
                fireCallbacksForDay(m_activeTradingDay);
            }
        } else if (!m_eodWindowWarned.exchange(true)) {
            // 窗口未配置 → 不发射 (不用硬编码时间顶替), 一次性 WARN
            INTERNAL_WARN_STREAM << "[MarketData] EOD 回调窗口未配置 "
                                 << "(eodCallbackStartTime/eodCallbackEndTime), 预收盘回调不发射";
        }
    }

    // ── 日切触发: 跳过已在预收盘评估过的交易日 ──
    if (dayChanged && prevTradingDay > 0
        && prevTradingDay != m_lastEvalTradingDay) {
        m_lastEvalTradingDay = prevTradingDay;
        fireCallbacksForDay(prevTradingDay);
    }
}

const LiveData& MarketDataService::liveData(const std::string& symbol) const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_.find(symbol);
    if (it != data_.end()) return it->second;

    auto [ins, _] = data_.emplace(symbol, LiveData(symbol));
    return ins->second;
}

LiveData& MarketDataService::mutableLiveData(const std::string& symbol)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    auto& d = data_[symbol];
    if (!d.valid()) {
        d = LiveData(symbol);
    }
    return d;
}

std::vector<std::string> MarketDataService::symbols() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    out.reserve(data_.size());
    for (const auto& [sym, _] : data_) out.push_back(sym);
    return out;
}

} // namespace domain::market
