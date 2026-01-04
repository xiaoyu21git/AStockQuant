#pragma once
#include "SimpleMovingAverage.h"
#include "Bar.h"
#include "SignalType.h"

namespace domain {

class CrossSignal {
public:
    CrossSignal(std::size_t fastPeriod,
                std::size_t slowPeriod);

    domain::SignalType update(const domain::model::Bar& bar);

private:
    SimpleMovingAverage m_fastMA;
    SimpleMovingAverage m_slowMA;

    int m_lastDiffSign = 0; // -1 / 0 / +1
};

}
