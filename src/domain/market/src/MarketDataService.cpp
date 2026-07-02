#include "MarketDataService.h"
#include "../../../engine/include/GmSessionEngine.h"

#include <ctime>
#include <sstream>

namespace domain::market {

MarketDataService& MarketDataService::instance()
{
    static MarketDataService s_instance;
    return s_instance;
}

void MarketDataService::registerEndOfDayCallback(EndOfDayCallback cb)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    m_eodCallbacks.push_back(std::move(cb));
}

void MarketDataService::fireCallbacksForDay(std::int64_t day)
{
    if (day <= 0) return;
    // 拷贝回调列表（持有锁），释放锁后调用
    std::vector<EndOfDayCallback> callbacks;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (m_eodCallbacks.empty()) return;
        callbacks = m_eodCallbacks;
    }
    std::string dayStr = std::to_string(day);
    for (auto& cb : callbacks) {
        cb(dayStr);
    }
}

void MarketDataService::onTick(const engine::GmTickData& td)
{
    if (td.symbol.empty()) return;

    std::int64_t prevTradingDay = 0;
    bool dayChanged = false;

    {
        const std::lock_guard<std::mutex> lock(mutex_);

        // ── tradingDay 变更检测 ──
        if (td.tradingDay > 0 && m_activeTradingDay > 0
            && td.tradingDay != m_activeTradingDay) {
            prevTradingDay = m_activeTradingDay;
            dayChanged = true;
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
    // 计算 tick 本地时间
    {
        auto tt = static_cast<time_t>(td.createdAt);
        struct tm local;
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&local, &tt);
#else
        localtime_r(&tt, &local);
#endif
        int minutes = local.tm_hour * 60 + local.tm_min;

        // ── 预收盘触发: 14:50-15:00, 当前交易日 ──
        if (minutes >= 890 && minutes < 900
            && m_activeTradingDay > 0
            && m_activeTradingDay != m_lastEvalTradingDay) {
            m_lastEvalTradingDay = m_activeTradingDay;
            fireCallbacksForDay(m_activeTradingDay);
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
