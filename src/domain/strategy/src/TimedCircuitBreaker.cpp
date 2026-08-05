#include "TimedCircuitBreaker.h"

#include <algorithm>

namespace domain::strategy {

bool TimedCircuitBreaker::checkIntraday(double currentEquity) {
    if (m_state.tradingHalted) return false;  // 已熔断, 不需要再减

    // 更新峰值
    if (currentEquity > m_state.peakEquity)
        m_state.peakEquity = currentEquity;

    // 回撤熔断
    if (m_state.peakEquity > 0.0) {
        double drawdown = 1.0 - currentEquity / m_state.peakEquity;
        if (drawdown >= m_cfg.maxDrawdown) {
            m_state.tradingHalted = true;
            m_state.haltDaysRemaining = m_cfg.haltDays;
            return true;
        }
    }

    // 单日亏损检查
    if (!m_state.dailyReduced && m_state.dailyStartEquity > 0.0) {
        double dailyLoss = 1.0 - currentEquity / m_state.dailyStartEquity;
        if (dailyLoss >= m_cfg.dailyLossLimit) {
            m_state.dailyReduced = true;
            return true;  // 通知调用方减仓
        }
    }

    return false;
}

void TimedCircuitBreaker::updateEndOfDay(double endOfDayEquity) {
    // 更新历史峰值
    if (endOfDayEquity > m_state.peakEquity)
        m_state.peakEquity = endOfDayEquity;

    // 熔断倒计时
    if (m_state.tradingHalted) {
        if (--m_state.haltDaysRemaining <= 0) {
            m_state.tradingHalted = false;
            m_state.peakEquity = endOfDayEquity;  // 重置峰值, 以当前净值重新开始
        }
    }

    // 重置日内状态 (为下一个交易日准备)
    m_state.dailyStartEquity = endOfDayEquity;
    m_state.dailyReduced = false;
}

double TimedCircuitBreaker::targetExposure() const {
    if (m_state.tradingHalted) return 0.0;
    if (m_state.dailyReduced)  return m_cfg.reduceTo;
    return 1.0;
}

void TimedCircuitBreaker::reset() {
    m_state = State{};
}

} // namespace domain::strategy
