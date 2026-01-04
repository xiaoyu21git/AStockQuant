#pragma once
#include <string>

namespace engine {

struct TradeRecord {
    std::string strategyName; // 策略名
    std::string symbol;       // 标的代码
    std::string time;         // 时间字符串
    double price;             // 价格
    bool isBuy;               // 是否买入
};

} // namespace engine
