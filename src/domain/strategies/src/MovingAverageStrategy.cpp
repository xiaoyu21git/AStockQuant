#include "MovingAverageStrategy.h"

namespace domain {

MovingAverageStrategy::MovingAverageStrategy(CrossSignal& signal)
    : m_signal(signal)
{
}

StrategyAction MovingAverageStrategy::onBar(const model::Bar& bar)
{
    SignalType s = m_signal.update(bar);

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

}
