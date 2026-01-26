#include "MovingAverageStrategy.h"

namespace domain {

MovingAverageStrategy::MovingAverageStrategy(CrossSignal& signal)
    : m_signal(signal)
{
}

StrategyAction MovingAverageStrategy::onBar(const domain::model::Bar& bar)  
{
        auto s = m_signal.update(bar); // 返回 SignalType

        if (!m_hasPosition && s == SignalType::Buy) {
            m_hasPosition = true;
            emitEvent(StrategyEventType::PositionOpened,
                      "Open long at price " + std::to_string(bar.close));
            return StrategyAction::OpenLong;
        }
        else if (m_hasPosition && s == SignalType::Sell) {
            m_hasPosition = false;
            emitEvent(StrategyEventType::PositionClosed,
                      "Close long at price " + std::to_string(bar.close));
            return StrategyAction::CloseLong;
        }

        return StrategyAction::None;
    }
std::string MovingAverageStrategy::name() const {
    return "MovingAverageStrategy";
}

}
