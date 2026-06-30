#pragma once
// ═════════════════════════════════════════════════════════════════════════
// LiveData — 单标的实时行情数据结构 (纯 C++，零 Qt)
//
// Bar       : 一根 K 线 (O/H/L/C/V/Amount)
// BarSeries : K 线序列 + 派生计算 (MA/EMA/VWAP/MACD/KDJ/RSI)
// LiveData  : 一个标的的完整实时数据 (日K + 多周期分钟K)
// ═════════════════════════════════════════════════════════════════════════

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace domain::market {

// ─── 一根 K 线 ──────────────────────────────────────────────────────────

class Bar final {
public:
    Bar() = default;

    // ── 价格 ──
    [[nodiscard]] double open()      const noexcept { return open_; }
    [[nodiscard]] double high()      const noexcept { return high_; }
    [[nodiscard]] double low()       const noexcept { return low_; }
    [[nodiscard]] double close()     const noexcept { return close_; }
    [[nodiscard]] double volume()    const noexcept { return volume_; }
    [[nodiscard]] double amount()    const noexcept { return amount_; }
    [[nodiscard]] std::int64_t timeBegin() const noexcept { return timeBegin_; }
    [[nodiscard]] bool   isAuction() const noexcept { return isAuction_; }

    void setOpen(double v)  noexcept { open_  = v; }
    void setHigh(double v)  noexcept { high_  = v; }
    void setLow(double v)   noexcept { low_   = v; }
    void setClose(double v) noexcept { close_ = v; }
    void setVolume(double v)    noexcept { volume_ = v; }
    void setAmount(double v)    noexcept { amount_ = v; }
    void setTimeBegin(std::int64_t v) noexcept { timeBegin_ = v; }
    void setIsAuction(bool v)   noexcept { isAuction_ = v; }

private:
    double open_   = 0.0;
    double high_   = 0.0;
    double low_    = 0.0;
    double close_  = 0.0;
    double volume_ = 0.0;
    double amount_ = 0.0;
    std::int64_t timeBegin_ = 0;
    bool  isAuction_ = false;
};

// ─── 盘口快照 ────────────────────────────────────────────────────────────

class DepthSnapshot final {
public:
    DepthSnapshot() = default;

    [[nodiscard]] int  levelCount() const noexcept { return levelCount_; }
    [[nodiscard]] double bidPrice(int level)  const noexcept { return bidPrices_[level]; }
    [[nodiscard]] double bidVolume(int level) const noexcept { return bidVolumes_[level]; }
    [[nodiscard]] double askPrice(int level)  const noexcept { return askPrices_[level]; }
    [[nodiscard]] double askVolume(int level) const noexcept { return askVolumes_[level]; }
    [[nodiscard]] std::int64_t updateTime() const noexcept { return updateTime_; }

    void setLevelCount(int n)  noexcept { levelCount_ = n; }
    void setBidPrice(int i, double v)  noexcept { bidPrices_[i] = v; }
    void setBidVolume(int i, double v) noexcept { bidVolumes_[i] = v; }
    void setAskPrice(int i, double v)  noexcept { askPrices_[i] = v; }
    void setAskVolume(int i, double v) noexcept { askVolumes_[i] = v; }
    void setUpdateTime(std::int64_t v) noexcept { updateTime_ = v; }

private:
    int      levelCount_ = 0;
    double   bidPrices_[5]{};
    double   bidVolumes_[5]{};
    double   askPrices_[5]{};
    double   askVolumes_[5]{};
    std::int64_t updateTime_ = 0;
};

// ─── 指标结果结构体 ─────────────────────────────────────────────────────

/// @brief MACD 指标结果
struct MacdData final {
    double dif = 0.0;
    double dea = 0.0;
    double histogram = 0.0;  // 2 * (DIF - DEA)
};

/// @brief KDJ 指标结果
struct KdjData final {
    double k = 50.0;
    double d = 50.0;
    double j = 50.0;
};

// ─── K 线序列 ───────────────────────────────────────────────────────────

/// @brief 一组 Bar 的序列，自动维护环形窗口，提供派生统计。
///
/// 均线、最高、最低等只从内存中的 Bar 计算，不依赖外部。
///
/// 缓存策略（混合模式）:
///   - push()     → 增量递推 EMA/MA/滚动和 (O(1))
///   - updateLast() → 全量重算最后 N 根 (EMA 是 IIR 滤波器,不可增量修正)
///   - replaceAll() → 全量重建所有缓存
class BarSeries final {
public:
    explicit BarSeries(int maxBars = 240);

    /// @brief 追加一根新 K 线（新周期开始）→ 增量更新所有缓存
    void push(Bar bar);

    /// @brief 更新当前最后一根 K 线（同周期内 tick 更新）→ 全量重算缓存
    void updateLast(double price, double volume, double amount, bool isAuction = false);

    /// @brief 全量替换所有 K 线（周期切换/复权场景）→ 全量重建缓存
    void replaceAll(const std::deque<Bar>& bars);

    /// @brief 清空所有数据和缓存
    void clear();

    // ── 查询 ──

    [[nodiscard]] int  count()            const noexcept { return static_cast<int>(bars_.size()); }
    [[nodiscard]] bool empty()            const noexcept { return bars_.empty(); }
    [[nodiscard]] const Bar& latest()     const;
    [[nodiscard]] const std::deque<Bar>& all() const { return bars_; }

    /// @brief 最近 n 根收盘价简单移动均值 (SMA)
    [[nodiscard]] double maClose(int n) const;

    /// @brief 最近 n 根收盘价指数移动均值 (EMA), α = 2/(n+1)
    [[nodiscard]] double emaClose(int n) const;

    /// @brief 最近 n 根的最高价
    [[nodiscard]] double high(int n) const;

    /// @brief 最近 n 根的最低价
    [[nodiscard]] double low(int n) const;

    /// @brief 最近 n 根量能合计
    [[nodiscard]] double volumeSum(int n) const;

    /// @brief 最近 n 根成交额合计
    [[nodiscard]] double amountSum(int n) const;

    /// @brief 成交量加权均价 VWAP = Σ(amount) / Σ(volume)
    /// @param n 窗口大小，n<=0 表示全量
    [[nodiscard]] double vwap(int n = -1) const;

    /// @brief 成交量均线 SMA(volume, n)
    [[nodiscard]] double maVolume(int n) const;

    /// @brief 振幅 = (high - low) / open (当前 Bar)
    [[nodiscard]] double amplitude() const;

    /// @brief MACD 指标 (参数可调)
    [[nodiscard]] MacdData macd(int fast = 12, int slow = 26, int signal = 9) const;

    /// @brief KDJ 指标 (参数可调)
    [[nodiscard]] KdjData kdj(int n = 9, int m1 = 3, int m2 = 3) const;

    /// @brief RSI 指标
    [[nodiscard]] double rsi(int n = 6) const;

private:
    // ── 编译期常量 ──
    static constexpr int kMaxMaWindow = 60;
    static constexpr int kMaPeriodCount = 4;
    static constexpr int kMaVolPeriodCount = 2;
    static constexpr int kEmaMacdCount = 3;  // EMA12/EMA26/DEA(EMA9)
    static constexpr int kMaPeriods[kMaPeriodCount] = {5, 10, 20, 60};
    static constexpr int kMaVolPeriods[kMaVolPeriodCount] = {5, 10};
    static constexpr int kEmaMacdParams[kEmaMacdCount] = {12, 26, 9};

    // ── 原始数据 ──
    std::deque<Bar> bars_;
    int maxBars_;

    // ── 滚动窗口缓存 (仅存最近 kMaxMaWindow 个值) ──
    std::deque<double> m_closeDeque_;
    std::deque<double> m_volumeDeque_;
    std::deque<double> m_highDeque_;
    std::deque<double> m_lowDeque_;
    std::deque<double> m_amountDeque_;

    // ── SMA 滚动和缓存 ──
    std::array<double, kMaPeriodCount> m_maSum_{};      // MA5/10/20/60
    std::array<double, kMaVolPeriodCount> m_maVolSum_{}; // MAVOL5/10

    // ── EMA 缓存 (push 时增量更新, updateLast 时全量重算) ──
    std::array<double, kMaPeriodCount> m_emaMa_{};      // EMA5/10/20/60
    std::array<double, kEmaMacdCount> m_emaMacd_{};     // EMA12/EMA26/DEA_EMA9
    bool m_emaValid_ = false;

    // ── 内部方法 ──
    int clampN(int n) const noexcept;

    /// @brief 遍历 m_closeDeque_ 全量重建 SMA 滚动和 + EMA 递推值
    void rebuildAllCaches();

    /// @brief 增量更新 SMA 滚动和 (push 新 Bar 时调用, O(1))
    void incrementSmaCaches(double newClose, double newVolume);

    /// @brief 增量更新 EMA 递推值 (push 新 Bar 时调用, O(1))
    void incrementEmaCaches(double newClose);

    /// @brief 获取 EMA 缓存值（按 period 查找）
    [[nodiscard]] double emaCached(int period) const noexcept;

    /// @brief 获取 SMA 缓存值（按 period 查找）
    [[nodiscard]] double smaCached(int period) const noexcept;

    /// @brief 获取 MAVOL 缓存值（按 period 查找）
    [[nodiscard]] double maVolCached(int period) const noexcept;
};

// ─── 单标的实时数据 ─────────────────────────────────────────────────────

/// @brief 一个标的的完整实时行情
///
/// daily 由 tick 持续更新，各分钟周期按需创建。
/// 派生指标由 BarSeries 内部计算，不额外存储。
class LiveData final {
public:
    LiveData() = default;
    explicit LiveData(const std::string& symbol);

    [[nodiscard]] const std::string& symbol() const noexcept { return symbol_; }
    [[nodiscard]] bool valid() const noexcept { return !symbol_.empty(); }

    // ── 日K（当日实时）──

    [[nodiscard]] Bar&             dailyBar()       noexcept { return daily_; }
    [[nodiscard]] const Bar&       dailyBar() const noexcept { return daily_; }
    [[nodiscard]] DepthSnapshot&   depth()          noexcept { return depth_; }
    [[nodiscard]] const DepthSnapshot& depth() const noexcept { return depth_; }

    [[nodiscard]] double preClose()  const noexcept { return preClose_; }
    [[nodiscard]] double limitUp()   const noexcept { return limitUp_; }
    [[nodiscard]] double limitDown() const noexcept { return limitDown_; }

    void setPreClose(double v)  noexcept { preClose_ = v; }
    void setLimitUp(double v)   noexcept { limitUp_ = v; }
    void setLimitDown(double v) noexcept { limitDown_ = v; }

    /// @brief 分时均线 = daily.amount / daily.volume (真实 VWAP, 同花顺标准)
    [[nodiscard]] double avgLine() const;

    // ── 分钟K（按需创建）──

    /// @brief 获取或创建指定分钟的 K 线序列
    [[nodiscard]] BarSeries&       period(int periodMinutes);
    [[nodiscard]] const BarSeries& period(int periodMinutes) const;

    /// @brief 已创建的周期列表
    [[nodiscard]] std::vector<int> availablePeriods() const;

private:
    std::string symbol_;
    Bar          daily_;
    DepthSnapshot depth_;
    double       preClose_  = 0.0;
    double       limitUp_   = 0.0;
    double       limitDown_ = 0.0;
    std::map<int, BarSeries> periods_;

    friend class MarketDataService;
};

} // namespace domain::market
