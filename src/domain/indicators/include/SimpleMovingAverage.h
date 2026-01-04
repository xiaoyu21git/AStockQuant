#pragma once
#include <deque>
#include <cstddef>

namespace domain {

class SimpleMovingAverage {
public:
    explicit SimpleMovingAverage(std::size_t period);

    void update(double price);
    bool isReady() const;
    double value() const;

private:
    std::size_t m_period;
    std::deque<double> m_window;
    double m_sum = 0.0;
};

}
