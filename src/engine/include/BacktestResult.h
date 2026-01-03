#pragma once
#include <vector>
#include <string>

namespace engine {

struct TradeRecord
{
    std::string time;
    double price;
    bool isBuy;
};

struct BacktestResult
{
    int tradeCount = 0;
    double pnl = 0.0;
    std::vector<TradeRecord> trades;
};

}
