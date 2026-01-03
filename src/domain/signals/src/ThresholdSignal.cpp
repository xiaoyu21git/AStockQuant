#include "ThresholdSignal.h"

namespace domain {

ThresholdSignal::ThresholdSignal(Indicator& indicator,
                                 double buyLevel,
                                 double sellLevel)
    : m_indicator(indicator)
    , m_buyLevel(buyLevel)
    , m_sellLevel(sellLevel)
{
}

domain::signals::SignalType ThresholdSignal::update(const domain::model::Bar& bar)
{
    m_indicator.update(bar);

    if (!m_indicator.ready())
        return domain::signals::SignalType::None;

    double v = m_indicator.value();

    if (v < m_buyLevel)
        return domain::signals::SignalType::Buy;

    if (v > m_sellLevel)
        return domain::signals::SignalType::Sell;

    return domain::signals::SignalType::None;
}

}
