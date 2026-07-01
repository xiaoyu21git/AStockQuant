#include "MarketDataService.h"
#include "../../../engine/include/GmSessionEngine.h"
#include "foundation/log/logging.hpp"

#include <atomic>
#include <ctime>
#include <sstream>
#include <thread>

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

bool MarketDataService::isInContinuousAuction()
{
    auto now = std::time(nullptr);
    std::tm local;
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    int hm = local.tm_hour * 100 + local.tm_min;
    int wd = local.tm_wday; // 0=Sun
    if (wd == 0 || wd == 6) return false;
    return (hm >= 930 && hm < 1130) || (hm >= 1300 && hm < 1500);
}

void MarketDataService::recoverTodayFromHistory(
    const std::vector<std::string>& symbols, int workers)
{
    if (!isInContinuousAuction()) {
        INTERNAL_INFO_STREAM << "[Recovery] 非交易时段，跳过分钟线恢复";
        return;
    }
    if (symbols.empty()) return;

    // 计算今日 09:30 epoch
    auto now = std::time(nullptr);
    std::tm local;
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    local.tm_hour = 9; local.tm_min = 30; local.tm_sec = 0;
    int64_t startEpoch = static_cast<int64_t>(std::mktime(&local));
    int64_t endEpoch   = static_cast<int64_t>(now);

    // 过滤已有数据的标的
    std::vector<std::string> needRecovery;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& sym : symbols) {
            auto it = data_.find(sym);
            if (it == data_.end() || !it->second.valid()
                || it->second.dailyBar().close() <= 0) {
                needRecovery.push_back(sym);
            }
        }
    }

    if (needRecovery.empty()) {
        INTERNAL_INFO_STREAM << "[Recovery] 所有标的数据已就绪，跳过恢复";
        return;
    }

    INTERNAL_INFO_STREAM << "[Recovery] 开始恢复 " << needRecovery.size()
                         << " 只标 (跳过 " << (symbols.size() - needRecovery.size())
                         << " 只已有数据)";

    std::atomic<size_t> done{0};
    std::atomic<size_t> totalBars{0};
    auto& engine = engine::GmSessionEngine::instance();

    auto worker = [&](size_t start, size_t end) {
        for (size_t i = start; i < end; ++i) {
            const auto& sym = needRecovery[i];
            auto bars = engine.fetchMinuteHistory(sym, startEpoch, endEpoch);
            if (bars.empty()) continue;

            totalBars += bars.size();
            for (auto& td : bars) {
                onTick(td);
                // 避免锁竞争：每 100 条 tick 短暂让出
            }
            ++done;
            if (done % 200 == 0) {
                INTERNAL_INFO_STREAM << "[Recovery] 进度 " << done.load()
                                     << "/" << needRecovery.size();
            }
        }
    };

    size_t n = needRecovery.size();
    size_t chunk = (n + workers - 1) / workers;
    std::vector<std::thread> threads;
    for (int w = 0; w < workers; ++w) {
        size_t start = w * chunk;
        size_t end   = std::min(start + chunk, n);
        if (start >= n) break;
        threads.emplace_back(worker, start, end);
    }
    for (auto& t : threads) t.join();

    INTERNAL_INFO_STREAM << "[Recovery] 完成: " << done.load() << " 只标, "
                         << totalBars.load() << " 根分钟线回放完毕";
}

} // namespace domain::market
