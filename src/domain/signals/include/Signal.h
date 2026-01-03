#pragma once
#include "Bar.h"
#include "SignalType.h"

namespace domain {

class Signal
{
public:
    virtual ~Signal() = default;

    // 每来一根K线，判断是否产生信号
    virtual domain::signals::SignalType update(const domain::model::Bar& bar) = 0;
};

}
