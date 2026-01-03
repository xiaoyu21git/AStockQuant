#include "SMA.h"

namespace domain {

SMA::SMA(int period)
    : m_period(period)
{
}

bool SMA::update(const domain::model::Bar& bar)
{
    m_window.push_back(bar.close);
    m_sum += bar.close;

    if (m_window.size() > m_period) {
        m_sum -= m_window.front();
        m_window.pop_front();
    }

    return ready();
}

double SMA::value() const
{
    if (!ready()) return 0.0;
    return m_sum / m_period;
}

bool SMA::ready() const
{
    return m_window.size() == m_period;
}

}
