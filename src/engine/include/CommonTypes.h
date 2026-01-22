// include/CommonTypes.h
#pragma once

#include <string>

struct MarketData {
    std::string symbol;
    double price;
    std::string timestamp;
};

enum class OrderSide {
    BUY,
    SELL
};

struct Order {
    std::string symbol;
    OrderSide side;
    int quantity;
    double price;
    std::string timestamp;
};

// include/IStrategy.h
#pragma once

#include "CommonTypes.h"

class IStrategy {
public:
    virtual ~IStrategy() = default;
    virtual void onMarketData(const MarketData& data) = 0;
    virtual void onOrderFilled(const Order& order) = 0;
};

// include/BacktestResult.h
#pragma once

struct BacktestResult {
    double initial_capital = 0.0;
    double final_capital = 0.0;
    double total_return = 0.0;
    double annualized_return = 0.0;
    double max_drawdown = 0.0;
    double sharpe_ratio = 0.0;
    int total_trades = 0;
    double win_rate = 0.0;
    // 可以添加更多指标...
};