#include "CrossSignal.h"

namespace domain {

CrossSignal::CrossSignal(domain::Indicator& fast, domain::Indicator& slow)
    : m_fast(fast)
    , m_slow(slow)
{
}

domain::signals::SignalType CrossSignal::update(const domain::model::Bar& bar)
{
    m_fast.update(bar);
    m_slow.update(bar);

    if (!m_fast.ready() || !m_slow.ready()) {
        return domain::signals::SignalType::None;
    }

    double diff = m_fast.value() - m_slow.value();

    if (!m_initialized) {
        m_prevDiff = diff;
        m_initialized = true;
        return domain::signals::SignalType::None;
    }

    domain::signals::SignalType result = domain::signals::SignalType::None;

    // 金叉
    if (m_prevDiff <= 0 && diff > 0) {
        result = domain::signals::SignalType::Buy;
    }
    // 死叉
    else if (m_prevDiff >= 0 && diff < 0) {
        result = domain::signals::SignalType::Sell;
    }

    m_prevDiff = diff;
    return result;
}

}
