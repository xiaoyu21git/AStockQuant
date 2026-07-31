#pragma once
// TimedCircuitBreaker — 风控熔断器
// 规则: 回撤≥15% → 熔断5日; 单日浮亏≥3% → 减仓至50%

namespace domain::strategy {

class TimedCircuitBreaker {
public:
    struct Config {
        double maxDrawdown   = 0.15;  // 回撤熔断阈值
        int    haltDays      = 5;     // 熔断持续交易日
        double dailyLossLimit = 0.03; // 单日亏损减仓阈值
        double reduceTo      = 0.5;   // 减仓至多少比例
    };

    explicit TimedCircuitBreaker(const Config& cfg = Config{}) : m_cfg(cfg) {}

    /// @brief 是否在熔断中
    bool isHalted() const { return m_state.tradingHalted; }

    /// @brief 日内检查: 当前净值是否触发单日减仓或熔断
    /// @param currentEquity 当前净值
    /// @return true = 需要立即减仓
    bool checkIntraday(double currentEquity);

    /// @brief 每日收盘更新: 记录新高、减熔断天数
    void updateEndOfDay(double endOfDayEquity);

    /// @brief 获取目标仓位比例 (熔断=0, 减仓=reduceTo, 正常=1.0)
    double targetExposure() const;

    /// @brief 重置状态
    void reset();

    [[nodiscard]] const Config& config() const { return m_cfg; }

private:
    struct State {
        bool   tradingHalted = false;
        int    haltDaysRemaining = 0;
        double peakEquity = 0.0;
        double dailyStartEquity = 0.0;
        bool   dailyReduced = false;
    };

    Config m_cfg;
    State  m_state;
};

} // namespace domain::strategy
