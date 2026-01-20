#include <iostream>
#include <vector>

//#include "BacktestEngine.h"
#include "BacktestResult.h"

#include "Bar.h"
#include "CrossSignal.h"
#include "ConcreteMAStrategy.h"

int main()
{
    // // ===== 1. 构造测试行情 =====
    // std::vector<domain::model::Bar> bars = {
    //     {"TEST", 1, 100, 105, 95, 100, 1000},
    //     {"TEST", 2, 100, 106, 98, 102, 1000},
    //     {"TEST", 3, 102, 110, 101, 108, 1000},
    //     {"TEST", 4, 108, 112, 107, 110, 1000},
    //     {"TEST", 5, 110, 111, 105, 106, 1000},
    //     {"TEST", 6, 106, 107, 100, 101, 1000},
    //     {"TEST", 7, 101, 103, 99, 100, 1000}
    // };

    // // ===== 2. 构造信号 & 策略 =====
    // domain::CrossSignal crossSignal(3, 5);
    // //domain::MovingAverageStrategy strategy(crossSignal);

    // // ===== 3. 回测 =====
    // //engine::BacktestEngine engine;
    // engine::BacktestResult result = engine.run( bars);

    // // ===== 4. 输出结果 =====
    // std::cout << "Total Trades: " << result.tradeCount << "\n";
    // std::cout << "PnL: " << result.pnl << "\n";
    // std::cout << "Trade Records:\n";

    // for (const auto& trade : result.trades) {
    //     std::cout
    //         << "Strategy: " << trade.strategyName
    //         << ", Symbol: " << trade.symbol
    //         << ", Time: " << trade.time
    //         << ", Price: " << trade.price
    //         << ", Buy: " << (trade.isBuy ? "Yes" : "No")
    //         << "\n";
    // }

    return 0;
}
