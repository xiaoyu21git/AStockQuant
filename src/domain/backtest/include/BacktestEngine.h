#pragma once

#include <vector>
#include <string>

// BacktestResult定义在engine模块
#include "../../../engine/include/BacktestResult.h"
// Bar定义在domain::model模块
#include "../../model/include/Bar.h"

// 这里约定 BacktestEngine 是一个纯 C++ 回测执行器：
// 输入一串 Bar + 初始资金 + 策略名，输出 engine::BacktestResult
namespace engine {

class BacktestEngine {
public:
    BacktestEngine() = default;

    BacktestResult run(
        const std::vector<domain::model::Bar>& bars,
        double initial_capital,
        const std::string& strategy_name,
        double max_position_ratio,
        double commission_rate,
        double slippage_rate,
        double min_volume);
};

} // namespace engine
