#pragma once
// ═════════════════════════════════════════════════════════════════════════
// IOrderListener — 策略订单监听器接口
// 委托给 IStrategyService.h 中的定义（避免重复定义）
// ═════════════════════════════════════════════════════════════════════════

#include "IStrategyService.h"

// IOrderListener 定义在 IStrategyService.h 中
// 此头文件保留作为兼容入口点

namespace domain::strategy {
// using IOrderListener = IOrderListener; (定义已在 IStrategyService.h 中)
} // namespace domain::strategy
