#pragma once
#include "Indicator.h"
#include <deque>
#include "Bar.h"
namespace domain {

class SMA : public Indicator
{
public:
    explicit SMA(int period);

    bool update(const domain::model::Bar& bar) override;
    double value() const override;
    bool ready() const override;

private:
    int m_period;
    std::deque<double> m_window;
    double m_sum = 0.0;
};

}
