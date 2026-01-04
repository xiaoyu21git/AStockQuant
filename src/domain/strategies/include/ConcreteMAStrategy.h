#pragma once
#include "MovingAverageStrategy.h"

namespace domain {

class ConcreteMAStrategy : public MovingAverageStrategy {
public:
    explicit ConcreteMAStrategy(CrossSignal& signal)
        : MovingAverageStrategy(signal) {}

    // 实现纯虚函数
    StrategyAction onBar(const model::Bar& bar) override {
        auto s = m_signal.update(bar); // 假设 update 返回 signals::SignalType

        if (!m_hasPosition && s == SignalType::Buy) {
            m_hasPosition = true;
            return StrategyAction::OpenLong;
        }
        if (m_hasPosition && s == SignalType::Sell) {
            m_hasPosition = false;
            return StrategyAction::CloseLong;
        }
        return StrategyAction::None;
    }
};

} // namespace domain
