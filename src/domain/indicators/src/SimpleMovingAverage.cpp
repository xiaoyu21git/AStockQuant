#include "SimpleMovingAverage.h"

namespace domain {

SimpleMovingAverage::SimpleMovingAverage(std::size_t period)
    : m_period(period)
{
}

void SimpleMovingAverage::update(double price)
{
    m_window.push_back(price);
    m_sum += price;

    if (m_window.size() > m_period) {
        m_sum -= m_window.front();
        m_window.pop_front();
    }
}

bool SimpleMovingAverage::isReady() const
{
    return m_window.size() == m_period;
}

double SimpleMovingAverage::value() const
{
    if (m_window.empty()) return 0.0;
    return m_sum / m_window.size();
}

}
