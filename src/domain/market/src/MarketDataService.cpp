#include "MarketDataService.h"
#include "../../../engine/include/GmSessionEngine.h"

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

void MarketDataService::fireEndOfDayCallbacks(std::int64_t closedTradingDay)
{
    // 注意: 调用方已持有 mutex_
    if (m_eodCallbacks.empty()) return;

    std::string dayStr;
    {
        // tradingDay 是 YYYYMMDD 格式的整数, 转为字符串
        // 例: 20260701
        dayStr = std::to_string(closedTradingDay);
    }

    // 复制回调列表，避免回调内部注册/注销导致迭代器失效
    auto callbacks = m_eodCallbacks;
    // 释放锁再调回调（回调内部可能调 liveData 等需要锁的接口）
    // 但是 fireEndOfDayCallbacks 是在 onTick 的 lock 内被调用的...
    // 先 unlock 再调: 这里需要特殊处理
    // 实际上我们在 onTick 内部持有锁时调用, 回调不能调需要锁的 liveData
    // 所以改为: 先把回调列表拷出来, 释放锁, 再调用
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
    // ── 锁释放后再调回调 ──

    if (dayChanged && prevTradingDay > 0) {
        // 拷贝回调列表并调用（无需持有锁）
        std::vector<EndOfDayCallback> callbacks;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            callbacks = m_eodCallbacks;
        }
        std::string dayStr = std::to_string(prevTradingDay);
        for (auto& cb : callbacks) {
            cb(dayStr);
        }
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
