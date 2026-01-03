#pragma once
#include <vector>
#include "Bar.h"

namespace domain {

class Indicator
{
public:
    virtual ~Indicator() = default;

    // 输入一根K线，返回是否产生新值
    virtual bool update(const domain::model::Bar& bar) = 0;

    // 当前指标值
    virtual double value() const = 0;

    // 是否已经 ready（周期足够）
    virtual bool ready() const = 0;
};

}
