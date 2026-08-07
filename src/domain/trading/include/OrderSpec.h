#pragma once
// OrderSpec.h — 订单规格纯 POD，无业务逻辑，无策略域依赖
// 交易域 (domain::trading) 与策略域通过此 DTO 解耦

#include "TradingTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace domain::trading {

/// @brief 订单规格 — 纯数据传输对象，不含任何业务逻辑或策略域依赖
/// 组装逻辑统一由 IOrderBuilder 的便捷方法负责
struct OrderSpec {
    std::string symbol;
    OrderSide side{OrderSide::Buy};
    OrderType orderType{OrderType::Market};
    PositionEffect positionEffect{PositionEffect::Open};
    std::int64_t quantity{0};
    double price{0.0};
    double signalScore{0.5};
    double targetWeight{0.0};

    /// @brief 策略域特定元数据通过 extensions 透传，OrderBuilder 不解读
    /// Key 来自 ExtKey 枚举值，Value 为 double 类型
    using ExtensionKey = int;
    std::unordered_map<ExtensionKey, double> extensions;
};

} // namespace domain::trading
