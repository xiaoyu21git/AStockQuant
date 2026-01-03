#pragma once
#include "Bar.h"
#include "StrategyAction.h"

namespace domain {

class Strategy
{
public:
    virtual ~Strategy() = default;

    // 每根K线调用一次
    virtual StrategyAction onBar(const domain::model::Bar& bar) = 0;

    bool hasPosition() const { return m_hasPosition; }

protected:
    bool m_hasPosition = false;
};

}
