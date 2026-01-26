#pragma once
#include "SimpleMovingAverage.h"
#include "Bar.h"
#include "SignalType.h"

namespace domain {

class CrossSignal {
public:
    CrossSignal(std::size_t fastPeriod,std::size_t slowPeriod);


    // 每根 Bar 更新均线，返回 Buy / Sell / None
    domain::SignalType update(const domain::model::Bar& bar);

private:
    SimpleMovingAverage m_fastMA;
    SimpleMovingAverage m_slowMA;

    int m_lastDiffSign = 0; // -1 / 0 / +1 上一根 Bar 的差值符号
};

} // namespace domain
