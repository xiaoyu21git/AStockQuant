#pragma once
#include <vector>
#include "TradeRecord.h"

namespace engine {

struct BacktestResult {
    int tradeCount = 0;
    double pnl = 0.0;
    std::vector<TradeRecord> trades;
};

} // namespace engine
