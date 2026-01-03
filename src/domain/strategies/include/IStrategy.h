#pragma once

#include <vector>
#include <string>

#include "Bar.h"
#include "Signal.h"

namespace domain::strategies {

class IStrategy {
public:
    virtual ~IStrategy() = default;

    // 策略名称
    virtual std::string name() const = 0;

    // 输入最新一根 bar，输出 0~N 个信号
    virtual std::vector<domain::Signal>
    onBar(const domain::model::Bar& bar) = 0;

    // 生命周期控制（可选）
    virtual void reset() {}
};

} // namespace domain::strategies
