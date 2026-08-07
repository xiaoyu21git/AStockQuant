#pragma once
// TechnicalIndicators.h — 技术指标纯计算函数 (零Qt, header-only)
//
// 所有函数通过 accessor lambda 访问数据, 与底层数据结构解耦。
// - smaAt / emaAt / vwapAt: 任意行查询, O(n) per call
// - macdLatest / kdjLatest / rsiLatest: 递推指标, 仅末尾行计算
//
// 使用: 桥接层传 [this](int i) { return m_data[i].close; }
//       编译器内联展开, 等同于手写 for 循环。

#include "LiveData.h"  // MacdData, KdjData

#include <cmath>
#include <cstdint>
#include <limits>

namespace domain::market::indicators {

// ═══════════════════════════════════════════════════════════════════════════
// 点查询 — 在 data[row] 处计算指标值
// ═══════════════════════════════════════════════════════════════════════════

/// @brief 简单移动均值: values[row-n+1 .. row] 的算术平均
template<typename A>
inline double smaAt(A&& getValue, int row, int n) noexcept {
    if (row < n - 1 || n <= 0)
        return std::numeric_limits<double>::quiet_NaN();
    double sum = 0.0;
    for (int i = row - n + 1; i <= row; ++i)
        sum += getValue(i);
    return sum / n;
}

/// @brief 指数移动均值: α=2/(n+1), 从 values[0] 递推到 values[row]
template<typename A>
inline double emaAt(A&& getValue, int row, int n) noexcept {
    if (row < n - 1 || n <= 0)
        return std::numeric_limits<double>::quiet_NaN();
    double alpha = 2.0 / (n + 1.0);
    double ema = getValue(0);
    for (int i = 1; i <= row; ++i)
        ema += alpha * (getValue(i) - ema);
    return ema;
}

/// @brief VWAP: Σ(close×volume) / Σ(volume), 从 0 累加到 row
/// @note 用 close*volume 近似 amount (CandleItem 无 amount 字段)
template<typename CA, typename VA>
inline double vwapAt(CA&& getClose, VA&& getVolume, int row) noexcept {
    if (row < 0)
        return std::numeric_limits<double>::quiet_NaN();
    double sumAmt = 0.0, sumVol = 0.0;
    for (int i = 0; i <= row; ++i) {
        sumAmt += getClose(i) * getVolume(i);
        sumVol += getVolume(i);
    }
    return sumVol > 0.0 ? sumAmt / sumVol : std::numeric_limits<double>::quiet_NaN();
}

// ═══════════════════════════════════════════════════════════════════════════
// 递推指标 — MACD / KDJ / RSI
// ═══════════════════════════════════════════════════════════════════════════

/// @brief MACD(fast, slow, signal) — 默认 MACD(12,26,9)
template<typename A>
inline MacdData macdLatest(A&& getClose, int count,
                           int fast = 12, int slow = 26, int signal = 9) noexcept {
    MacdData result{};
    if (count < slow) return result;

    double aFast = 2.0 / (fast + 1.0);
    double aSlow = 2.0 / (slow + 1.0);
    double aSig  = 2.0 / (signal + 1.0);

    double emaFast = getClose(0);
    double emaSlow = getClose(0);
    double dea = 0.0;
    bool deaInit = false;

    for (int i = 1; i < count; ++i) {
        double c = getClose(i);
        emaFast += aFast * (c - emaFast);
        emaSlow += aSlow * (c - emaSlow);
        double dif = emaFast - emaSlow;
        if (!deaInit) { dea = dif; deaInit = true; }
        else dea += aSig * (dif - dea);

        if (i == count - 1) {
            result.dif = emaFast - emaSlow;
            result.dea = dea;
            result.histogram = 2.0 * (result.dif - result.dea);
        }
    }
    return result;
}

/// @brief KDJ(n, m1, m2) — 默认 KDJ(9,3,3)
template<typename CA, typename HA, typename LA>
inline KdjData kdjLatest(CA&& getClose, HA&& getHigh, LA&& getLow,
                         int count, int n = 9, int m1 = 3, int m2 = 3) noexcept {
    KdjData result{};
    if (count < n + m1) return result;

    double aK = 2.0 / (m1 + 1.0);
    double aD = 2.0 / (m2 + 1.0);
    double kval = 50.0;
    double dval = 50.0;

    for (int i = n - 1; i < count; ++i) {
        double hh = getHigh(i);
        double ll = getLow(i);
        for (int j = i - (n - 1); j < i; ++j) {
            if (getHigh(j) > hh) hh = getHigh(j);
            if (getLow(j)  < ll) ll = getLow(j);
        }
        double rsv = (hh - ll > 0.0)
            ? (getClose(i) - ll) / (hh - ll) * 100.0
            : 50.0;
        kval += aK * (rsv - kval);
        dval += aD * (kval - dval);
    }

    result.k = kval;
    result.d = dval;
    result.j = 3.0 * kval - 2.0 * dval;
    return result;
}

/// @brief RSI(n) — 默认 RSI(6), Wilder's smoothing
template<typename A>
inline double rsiLatest(A&& getClose, int count, int n = 6) noexcept {
    if (count < n + 1)
        return std::numeric_limits<double>::quiet_NaN();

    double avgGain = 0.0, avgLoss = 0.0;
    for (int i = 1; i <= n; ++i) {
        double diff = getClose(i) - getClose(i - 1);
        if (diff > 0) avgGain += diff;
        else          avgLoss += -diff;
    }
    avgGain /= n;
    avgLoss /= n;

    for (int i = n + 1; i < count; ++i) {
        double diff = getClose(i) - getClose(i - 1);
        double gain = (diff > 0) ? diff : 0.0;
        double loss = (diff > 0) ? 0.0 : -diff;
        avgGain = (avgGain * (n - 1) + gain) / n;
        avgLoss = (avgLoss * (n - 1) + loss) / n;
    }

    if (avgLoss == 0.0) return 100.0;
    return 100.0 - 100.0 / (1.0 + avgGain / avgLoss);
}

} // namespace domain::market::indicators
