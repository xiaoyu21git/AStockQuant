#include "CrossSignal.h"

namespace domain {

CrossSignal::CrossSignal(std::size_t fastPeriod,
                         std::size_t slowPeriod)
    : m_fastMA(fastPeriod),
      m_slowMA(slowPeriod)
{
}

SignalType CrossSignal::update(const domain::model::Bar& bar)
{
    m_fastMA.update(bar.close);
    m_slowMA.update(bar.close);

    if (!m_fastMA.isReady() || !m_slowMA.isReady()) {
        return SignalType::None;
    }

    double diff = m_fastMA.value() - m_slowMA.value();
    int sign = (diff > 0) ? 1 : (diff < 0 ? -1 : 0);

    SignalType signal = SignalType::None;

    if (m_lastDiffSign < 0 && sign > 0) {
        signal = SignalType::Buy;   // 金叉
    }
    else if (m_lastDiffSign > 0 && sign < 0) {
        signal = SignalType::Sell;  // 死叉
    }

    m_lastDiffSign = sign;
    return signal;
}

}
