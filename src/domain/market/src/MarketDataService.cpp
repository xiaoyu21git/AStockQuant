#include "MarketDataService.h"
#include "../../../engine/include/GmSessionEngine.h"

#include <sstream>

namespace domain::market {

MarketDataService& MarketDataService::instance()
{
    static MarketDataService s_instance;
    return s_instance;
}

std::uint64_t MarketDataService::registerEndOfDayCallback(EndOfDayCallback cb)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t token = m_eodCallbackNextToken++;
    m_eodCallbacks[token] = std::move(cb);
    return token;
}

void MarketDataService::unregisterEndOfDayCallback(std::uint64_t token)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    m_eodCallbacks.erase(token);
}

void MarketDataService::fireEndOfDayCallbacks(std::int64_t closedTradingDay)
{
    if (m_eodCallbacks.empty()) return;

    std::string dayStr = std::to_string(closedTradingDay);

    // 复制回调列表，避免回调内部注册/注销导致迭代器失效
    auto callbacks = m_eodCallbacks;
    for (auto& [token, cb] : callbacks) {
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

        // ── Tick 断点检测: 记录时间戳 ──
        auto now = Clock::now();
        m_lastGlobalTickTime = now;
        m_lastTickTimeBySymbol[td.symbol] = now;
        ++m_totalTicks;
    }
    // ── 锁释放后再调回调 ──

    if (dayChanged && prevTradingDay > 0) {
        // 拷贝回调列表并调用（无需持有锁）
        std::vector<EndOfDayCallback> callbacks;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            callbacks.reserve(m_eodCallbacks.size());
            for (const auto& [token, cb] : m_eodCallbacks) {
                callbacks.push_back(cb);
            }
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

double MarketDataService::secondsSinceLastTick() const noexcept
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (m_totalTicks == 0) return -1.0;
    auto now = Clock::now();
    return std::chrono::duration<double>(now - m_lastGlobalTickTime).count();
}

std::vector<std::pair<std::string, double>>
MarketDataService::tickStalledSymbols(double thresholdSec) const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, double>> stalled;
    auto now = Clock::now();
    for (const auto& [sym, lastTime] : m_lastTickTimeBySymbol) {
        double elapsed = std::chrono::duration<double>(now - lastTime).count();
        if (elapsed > thresholdSec) {
            stalled.emplace_back(sym, elapsed);
        }
    }
    return stalled;
}

} // namespace domain::market
