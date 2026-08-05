#include "LiveData.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace domain::market {

// ═════════════════════════════════════════════════════════════════════════
// BarSeries 实现
// ═════════════════════════════════════════════════════════════════════════

BarSeries::BarSeries(int maxBars) : maxBars_(maxBars)
{
    if (maxBars_ < 2) maxBars_ = 2;
}

// ─── push: 新增一根 K 线 → 增量更新所有缓存 ─────────────────────────────

void BarSeries::push(Bar bar)
{
    // ── 原始数据 ──
    bars_.push_back(std::move(bar));
    while (static_cast<int>(bars_.size()) > maxBars_) {
        bars_.pop_front();
    }

    const double newClose  = bars_.back().close();
    const double newVolume = bars_.back().volume();

    // ── 维护滚动窗口 deques ──
    m_closeDeque_.push_back(newClose);
    if (static_cast<int>(m_closeDeque_.size()) > kMaxMaWindow)
        m_closeDeque_.pop_front();

    m_volumeDeque_.push_back(newVolume);
    if (static_cast<int>(m_volumeDeque_.size()) > kMaxMaWindow)
        m_volumeDeque_.pop_front();

    m_highDeque_.push_back(bars_.back().high());
    if (static_cast<int>(m_highDeque_.size()) > kMaxMaWindow)
        m_highDeque_.pop_front();

    m_lowDeque_.push_back(bars_.back().low());
    if (static_cast<int>(m_lowDeque_.size()) > kMaxMaWindow)
        m_lowDeque_.pop_front();

    m_amountDeque_.push_back(bars_.back().amount());
    if (static_cast<int>(m_amountDeque_.size()) > kMaxMaWindow)
        m_amountDeque_.pop_front();

    // ── O(1) 增量更新 SMA 滚动和 ──
    incrementSmaCaches(newClose, newVolume);

    // ── O(1) 增量更新 EMA 递推值 ──
    incrementEmaCaches(newClose);
}

// ─── updateLast: 更新当前 Bar → 全量重算所有缓存 ─────────────────────────

void BarSeries::updateLast(double price, double volume, double amount, bool isAuction)
{
    if (bars_.empty()) {
        Bar b;
        b.setOpen(price);
        b.setHigh(price);
        b.setLow(price);
        b.setClose(price);
        b.setVolume(volume);
        b.setAmount(amount);
        b.setIsAuction(isAuction);
        push(std::move(b));
        return;
    }

    auto& cur = bars_.back();
    if (price > cur.high())  cur.setHigh(price);
    if (price < cur.low())   cur.setLow(price);
    cur.setClose(price);
    cur.setVolume(cur.volume() + volume);
    cur.setAmount(cur.amount() + amount);
    cur.setIsAuction(cur.isAuction() || isAuction);

    // 关键: updateLast 时同步更新滚动 deques 的最后一个元素
    if (!m_closeDeque_.empty())  m_closeDeque_.back()  = cur.close();
    if (!m_volumeDeque_.empty()) m_volumeDeque_.back() = cur.volume();
    if (!m_highDeque_.empty())   m_highDeque_.back()   = cur.high();
    if (!m_lowDeque_.empty())    m_lowDeque_.back()    = cur.low();
    if (!m_amountDeque_.empty()) m_amountDeque_.back() = cur.amount();

    // 关键: EMA 是 IIR 滤波器, updateLast 破坏 ema_prev 的纯净性
    // → 必须全量重算
    rebuildAllCaches();
}

// ─── replaceAll: 全量替换 → 全量重建缓存 ────────────────────────────────

void BarSeries::replaceAll(const std::deque<Bar>& bars)
{
    bars_.clear();
    m_closeDeque_.clear();
    m_volumeDeque_.clear();
    m_highDeque_.clear();
    m_lowDeque_.clear();
    m_amountDeque_.clear();

    // 重新填充
    for (const auto& b : bars) {
        bars_.push_back(b);
        m_closeDeque_.push_back(b.close());
        m_volumeDeque_.push_back(b.volume());
        m_highDeque_.push_back(b.high());
        m_lowDeque_.push_back(b.low());
        m_amountDeque_.push_back(b.amount());
    }

    // 裁剪 deques 到 kMaxMaWindow
    while (static_cast<int>(m_closeDeque_.size()) > kMaxMaWindow)
        m_closeDeque_.pop_front();
    while (static_cast<int>(m_volumeDeque_.size()) > kMaxMaWindow)
        m_volumeDeque_.pop_front();
    while (static_cast<int>(m_highDeque_.size()) > kMaxMaWindow)
        m_highDeque_.pop_front();
    while (static_cast<int>(m_lowDeque_.size()) > kMaxMaWindow)
        m_lowDeque_.pop_front();
    while (static_cast<int>(m_amountDeque_.size()) > kMaxMaWindow)
        m_amountDeque_.pop_front();

    while (static_cast<int>(bars_.size()) > maxBars_)
        bars_.pop_front();

    m_emaValid_ = false;
    rebuildAllCaches();
}

// ─── clear ──────────────────────────────────────────────────────────────

void BarSeries::clear()
{
    bars_.clear();
    m_closeDeque_.clear();
    m_volumeDeque_.clear();
    m_highDeque_.clear();
    m_lowDeque_.clear();
    m_amountDeque_.clear();
    std::fill(m_maSum_.begin(), m_maSum_.end(), 0.0);
    std::fill(m_maVolSum_.begin(), m_maVolSum_.end(), 0.0);
    std::fill(m_emaMa_.begin(), m_emaMa_.end(), 0.0);
    std::fill(m_emaMacd_.begin(), m_emaMacd_.end(), 0.0);
    m_emaValid_ = false;
}

// ─── 内部: 增量 SMA ─────────────────────────────────────────────────────

void BarSeries::incrementSmaCaches(double newClose, double newVolume)
{
    const int sz = static_cast<int>(m_closeDeque_.size());

    for (int i = 0; i < kMaPeriodCount; ++i) {
        int p = kMaPeriods[i];
        m_maSum_[i] += newClose;
        if (sz > p)
            m_maSum_[i] -= m_closeDeque_[static_cast<size_t>(sz - p - 1)];
    }

    const int vSz = static_cast<int>(m_volumeDeque_.size());
    for (int i = 0; i < kMaVolPeriodCount; ++i) {
        int p = kMaVolPeriods[i];
        m_maVolSum_[i] += newVolume;
        if (vSz > p)
            m_maVolSum_[i] -= m_volumeDeque_[static_cast<size_t>(vSz - p - 1)];
    }
}

// ─── 内部: 增量 EMA ─────────────────────────────────────────────────────

void BarSeries::incrementEmaCaches(double newClose)
{
    // EMA(close, n): α = 2/(n+1)
    for (int i = 0; i < kMaPeriodCount; ++i) {
        double alpha = 2.0 / (kMaPeriods[i] + 1.0);
        double prev = m_emaValid_ ? m_emaMa_[i] : newClose;
        m_emaMa_[i] = prev + alpha * (newClose - prev);
    }

    // EMA12/EMA26/EMA9 (MACD 用)
    for (int i = 0; i < kEmaMacdCount; ++i) {
        double alpha = 2.0 / (kEmaMacdParams[i] + 1.0);
        double prev = m_emaValid_ ? m_emaMacd_[i] : newClose;
        m_emaMacd_[i] = prev + alpha * (newClose - prev);
    }

    m_emaValid_ = true;
}

// ─── 内部: 全量重算所有缓存 ─────────────────────────────────────────────

void BarSeries::rebuildAllCaches()
{
    const int sz = static_cast<int>(m_closeDeque_.size());
    if (sz == 0) return;

    // ── 重算 SMA 滚动和 ──
    // 从头遍历 m_closeDeque_, 构建各周期的滚动和
    // 对于 MA(n): sum = last n 个 close 的和
    std::fill(m_maSum_.begin(), m_maSum_.end(), 0.0);
    for (int i = 0; i < kMaPeriodCount; ++i) {
        int p = kMaPeriods[i];
        int start = (sz > p) ? (sz - p) : 0;
        for (int j = start; j < sz; ++j)
            m_maSum_[i] += m_closeDeque_[static_cast<size_t>(j)];
    }

    // ── 重算 MAVOL 滚动和 ──
    const int vSz = static_cast<int>(m_volumeDeque_.size());
    std::fill(m_maVolSum_.begin(), m_maVolSum_.end(), 0.0);
    for (int i = 0; i < kMaVolPeriodCount; ++i) {
        int p = kMaVolPeriods[i];
        int start = (vSz > p) ? (vSz - p) : 0;
        for (int j = start; j < vSz; ++j)
            m_maVolSum_[i] += m_volumeDeque_[static_cast<size_t>(j)];
    }

    // ── 从头递推 EMA ──
    std::fill(m_emaMa_.begin(), m_emaMa_.end(), 0.0);
    std::fill(m_emaMacd_.begin(), m_emaMacd_.end(), 0.0);

    for (int i = 0; i < sz; ++i) {
        double c = m_closeDeque_[static_cast<size_t>(i)];

        for (int j = 0; j < kMaPeriodCount; ++j) {
            double alpha = 2.0 / (kMaPeriods[j] + 1.0);
            double prev = (i == 0) ? c : m_emaMa_[j];
            m_emaMa_[j] = prev + alpha * (c - prev);
        }

        for (int j = 0; j < kEmaMacdCount; ++j) {
            double alpha = 2.0 / (kEmaMacdParams[j] + 1.0);
            double prev = (i == 0) ? c : m_emaMacd_[j];
            m_emaMacd_[j] = prev + alpha * (c - prev);
        }
    }

    m_emaValid_ = true;
}

// ─── 辅助 ───────────────────────────────────────────────────────────────

int BarSeries::clampN(int n) const noexcept
{
    int cnt = count();
    return (n <= 0 || n > cnt) ? cnt : n;
}

const Bar& BarSeries::latest() const
{
    if (bars_.empty()) {
        static const Bar s_empty{};
        return s_empty;
    }
    return bars_.back();
}

// ─── 缓存查询辅助 ───────────────────────────────────────────────────────

double BarSeries::smaCached(int period) const noexcept
{
    for (int i = 0; i < kMaPeriodCount; ++i) {
        if (kMaPeriods[i] == period) {
            int p = period;
            int sz = static_cast<int>(m_closeDeque_.size());
            if (sz < p) return std::numeric_limits<double>::quiet_NaN();
            return m_maSum_[i] / static_cast<double>(p);
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double BarSeries::emaCached(int period) const noexcept
{
    for (int i = 0; i < kMaPeriodCount; ++i) {
        if (kMaPeriods[i] == period) {
            if (!m_emaValid_ || m_closeDeque_.size() < static_cast<size_t>(period))
                return std::numeric_limits<double>::quiet_NaN();
            return m_emaMa_[i];
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double BarSeries::maVolCached(int period) const noexcept
{
    for (int i = 0; i < kMaVolPeriodCount; ++i) {
        if (kMaVolPeriods[i] == period) {
            int p = period;
            int sz = static_cast<int>(m_volumeDeque_.size());
            if (sz < p) return std::numeric_limits<double>::quiet_NaN();
            return m_maVolSum_[i] / static_cast<double>(p);
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

// ─── 公开查询方法 ───────────────────────────────────────────────────────

double BarSeries::maClose(int n) const
{
    double v = smaCached(n);
    if (std::isnan(v)) {
        // 回退: 如果 n 不在预计算列表中, 遍历计算
        n = clampN(n);
        if (n == 0) return 0.0;
        double sum = 0.0;
        auto it = bars_.rbegin();
        for (int i = 0; i < n; ++i, ++it)
            sum += it->close();
        return sum / n;
    }
    return v;
}

double BarSeries::emaClose(int n) const
{
    double v = emaCached(n);
    if (std::isnan(v)) {
        // 回退: 从头递推
        int cnt = count();
        if (cnt == 0) return 0.0;
        n = (n <= 0 || n > cnt) ? cnt : n;
        double alpha = 2.0 / (n + 1.0);
        double ema = bars_.front().close();
        for (auto it = std::next(bars_.begin()); it != bars_.end(); ++it) {
            ema = ema + alpha * (it->close() - ema);
        }
        return ema;
    }
    return v;
}

double BarSeries::maVolume(int n) const
{
    double v = maVolCached(n);
    if (std::isnan(v)) {
        n = clampN(n);
        if (n == 0) return 0.0;
        double sum = 0.0;
        auto it = bars_.rbegin();
        for (int i = 0; i < n; ++i, ++it)
            sum += it->volume();
        return sum / n;
    }
    return v;
}

double BarSeries::high(int n) const
{
    n = clampN(n);
    if (n == 0) return 0.0;
    double v = bars_.rbegin()->high();
    auto it = bars_.rbegin();
    for (int i = 0; i < n; ++i, ++it)
        if (it->high() > v) v = it->high();
    return v;
}

double BarSeries::low(int n) const
{
    n = clampN(n);
    if (n == 0) return 0.0;
    double v = bars_.rbegin()->low();
    auto it = bars_.rbegin();
    for (int i = 0; i < n; ++i, ++it)
        if (it->low() < v) v = it->low();
    return v;
}

double BarSeries::volumeSum(int n) const
{
    n = clampN(n);
    double sum = 0.0;
    auto it = bars_.rbegin();
    for (int i = 0; i < n; ++i, ++it)
        sum += it->volume();
    return sum;
}

double BarSeries::amountSum(int n) const
{
    n = clampN(n);
    double sum = 0.0;
    auto it = bars_.rbegin();
    for (int i = 0; i < n; ++i, ++it)
        sum += it->amount();
    return sum;
}

double BarSeries::vwap(int n) const
{
    // VWAP = Σ(amount) / Σ(volume)  同花顺标准
    if (n <= 0) {
        // 全量
        double sumAmt = 0.0, sumVol = 0.0;
        for (const auto& b : bars_) {
            sumAmt += b.amount();
            sumVol += b.volume();
        }
        return sumVol > 0.0 ? sumAmt / sumVol : 0.0;
    }

    n = clampN(n);
    if (n == 0) return 0.0;
    double sumAmt = 0.0, sumVol = 0.0;
    auto it = bars_.rbegin();
    for (int i = 0; i < n; ++i, ++it) {
        sumAmt += it->amount();
        sumVol += it->volume();
    }
    return sumVol > 0.0 ? sumAmt / sumVol : 0.0;
}

double BarSeries::amplitude() const
{
    if (bars_.empty()) return 0.0;
    const auto& b = bars_.back();
    if (b.open() <= 0.0) return 0.0;
    return (b.high() - b.low()) / b.open();
}

// ─── MACD ────────────────────────────────────────────────────────────────
// MACD 标准公式:
//   EMA12 = EMA(close, 12)
//   EMA26 = EMA(close, 26)
//   DIF   = EMA12 - EMA26
//   DEA   = EMA(DIF, 9)
//   HIST  = 2 * (DIF - DEA)

MacdData BarSeries::macd(int fast, int slow, int signal) const
{
    MacdData result;
    int cnt = count();
    if (cnt < slow) return result;  // 数据不足

    // 如果参数是默认值 (12/26/9), 优先使用缓存
    if (fast == 12 && slow == 26 && signal == 9) {
        double ema12 = m_emaMacd_[0];  // EMA12
        double ema26 = m_emaMacd_[1];  // EMA26
        double dea9  = m_emaMacd_[2];  // DEA = EMA(DIF, 9)
        result.dif = ema12 - ema26;
        result.dea = dea9;
        result.histogram = 2.0 * (result.dif - result.dea);
        return result;
    }

    // 非默认参数: 从头计算
    double alphaFast   = 2.0 / (fast + 1.0);
    double alphaSlow   = 2.0 / (slow + 1.0);
    double alphaSignal = 2.0 / (signal + 1.0);

    double emaFast = bars_.front().close();
    double emaSlow = bars_.front().close();
    double deaVal  = 0.0;
    bool deaInit = false;

    for (auto it = std::next(bars_.begin()); it != bars_.end(); ++it) {
        double c = it->close();
        emaFast = emaFast + alphaFast * (c - emaFast);
        emaSlow = emaSlow + alphaSlow * (c - emaSlow);
        double dif = emaFast - emaSlow;
        if (!deaInit) {
            deaVal  = dif;
            deaInit = true;
        } else {
            deaVal = deaVal + alphaSignal * (dif - deaVal);
        }
    }

    double dif = emaFast - emaSlow;
    result.dif = dif;
    result.dea = deaVal;
    result.histogram = 2.0 * (dif - deaVal);
    return result;
}

// ─── KDJ ─────────────────────────────────────────────────────────────────
// 标准公式:
//   RSV(n) = (close - low_n) / (high_n - low_n) * 100
//   K = 2/3 * prev_K + 1/3 * RSV  (即 EMA(RSV, m1) 的近似, m1=3)
//   D = 2/3 * prev_D + 1/3 * K
//   J = 3*K - 2*D

KdjData BarSeries::kdj(int n, int m1, int m2) const
{
    (void)m2;  // 同花顺标准 KDJ 中 D = SMA(K, m2), 这里用 EMA 近似
    KdjData result;
    int cnt = count();
    if (cnt < n + m1) return result;

    // 取最近 cnt 根计算最近一个 KDJ 值
    // KDJ 是递推指标, 必须从头计算
    double alphaK = 2.0 / (m1 + 1.0);  // 近似 SMA(K, m1)
    double alphaD = 2.0 / (m2 + 1.0);

    double kVal = 50.0;
    double dVal = 50.0;

    // 收集最近 n 根的 high/low 用于计算 RSV
    // 然后从第 n 根开始递推
    for (int i = n - 1; i < cnt; ++i) {
        // 计算最近 n 根的 highest high / lowest low
        double hh = bars_[static_cast<size_t>(i)].high();
        double ll = bars_[static_cast<size_t>(i)].low();
        for (int j = i - n + 1; j < i; ++j) {
            double h = bars_[static_cast<size_t>(j)].high();
            double l = bars_[static_cast<size_t>(j)].low();
            if (h > hh) hh = h;
            if (l < ll) ll = l;
        }
        double rsv = (hh - ll > 0.0)
            ? (bars_[static_cast<size_t>(i)].close() - ll) / (hh - ll) * 100.0
            : 50.0;

        kVal = kVal + alphaK * (rsv - kVal);
        dVal = dVal + alphaD * (kVal - dVal);
    }

    result.k = kVal;
    result.d = dVal;
    result.j = 3.0 * kVal - 2.0 * dVal;
    return result;
}

// ─── RSI ─────────────────────────────────────────────────────────────────
// RSI = 100 - 100 / (1 + avgGain / avgLoss)
// 使用 Wilder's smoothing: avgGain = (prev_avgGain * (n-1) + gain) / n

double BarSeries::rsi(int n) const
{
    int cnt = count();
    if (cnt < n + 1) return std::numeric_limits<double>::quiet_NaN();

    // 初始 avgGain / avgLoss (简单平均前 n 根)
    double avgGain = 0.0, avgLoss = 0.0;
    auto it = std::next(bars_.begin());
    for (int i = 0; i < n; ++i, ++it) {
        double diff = it->close() - std::prev(it)->close();
        if (diff > 0) avgGain += diff;
        else          avgLoss += -diff;
    }
    avgGain /= n;
    avgLoss /= n;

    // 递推 (Wilder's smoothing)
    for (; it != bars_.end(); ++it) {
        double diff = it->close() - std::prev(it)->close();
        double gain = (diff > 0) ? diff : 0.0;
        double loss = (diff > 0) ? 0.0 : -diff;
        avgGain = (avgGain * (n - 1) + gain) / n;
        avgLoss = (avgLoss * (n - 1) + loss) / n;
    }

    if (avgLoss == 0.0) return 100.0;
    double rs = avgGain / avgLoss;
    return 100.0 - 100.0 / (1.0 + rs);
}

// ═════════════════════════════════════════════════════════════════════════
// LiveData 实现
// ═════════════════════════════════════════════════════════════════════════

LiveData::LiveData(const std::string& symbol) : symbol_(symbol) {}

double LiveData::avgLine() const
{
    // 分时均价线 = 累计成交额 / 累计成交量 (同花顺标准)
    if (daily_.volume() <= 0.0) return 0.0;
    return daily_.amount() / daily_.volume();
}

BarSeries& LiveData::period(int periodMinutes)
{
    if (periodMinutes < 1) periodMinutes = 1;
    auto it = periods_.find(periodMinutes);
    if (it != periods_.end()) return it->second;

    int maxBars = (5 * 240) / periodMinutes;
    if (maxBars < 2) maxBars = 2;
    auto [ins, _] = periods_.emplace(periodMinutes, BarSeries(maxBars));
    return ins->second;
}

const BarSeries& LiveData::period(int periodMinutes) const
{
    static const BarSeries s_empty(0);
    auto it = periods_.find(periodMinutes);
    return (it != periods_.end()) ? it->second : s_empty;
}

std::vector<int> LiveData::availablePeriods() const
{
    std::vector<int> out;
    out.reserve(periods_.size());
    for (const auto& [k, _] : periods_) out.push_back(k);
    return out;
}

} // namespace domain::market
