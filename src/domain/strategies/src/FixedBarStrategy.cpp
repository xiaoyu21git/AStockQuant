#include "FixedBarStrategy.h"
namespace domain {

FixedBarStrategy::FixedBarStrategy(int buyBar, int sellBar)
    : m_buyBar(buyBar), m_sellBar(sellBar) {}

std::string FixedBarStrategy::name() const {
    return "FixedBarStrategy(" +
           std::to_string(m_buyBar) + "," +
           std::to_string(m_sellBar) + ")";
}

void FixedBarStrategy::onStart() {
    m_index = 0;
    m_positionOpen = false;
}

void FixedBarStrategy::onFinish() {
}

StrategyAction FixedBarStrategy::onBar(const model::Bar&) {
    ++m_index;

    if (!m_positionOpen && m_index == m_buyBar) {
        m_positionOpen = true;
        return StrategyAction::OpenLong;
    }

    if (m_positionOpen && m_index == m_sellBar) {
        m_positionOpen = false;
        return StrategyAction::CloseLong;
    }

    return StrategyAction::None;
}

} // namespace domain
