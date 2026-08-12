#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "../../types/ResolvedStrategyBehavior.h"
#include "StrategyDefinitionTypes.h"

namespace domain::strategies {

/// @brief 策略类型注册表 — StrategyType ↔ 枚举名字符串 / 行为类型 / 存储类型的唯一权威映射
///
/// 设计约定：
/// - C++ 枚举是唯一事实源；行为类型(StrategyBehaviorKind)一律由策略类型推导，不再单独存储
/// - 枚举名字符串只允许出现在受控边界（数据库落库文本字段、元数据 JSON 键、展示），禁止参与分发
/// - 解析严格精确匹配：缺失/非法直接返回 nullopt / 空串，无任何回退或兼容映射
class StrategyTypeRegistry final {
public:
    /// @brief 全部 11 种策略类型的固定顺序列表（与 UI 选择器展示顺序一致）
    [[nodiscard]] static constexpr std::array<StrategyType, 11> all() noexcept
    {
        return {
            StrategyType::DOUBLE_MOVING_AVERAGE,
            StrategyType::TURTLE_BREAKOUT,
            StrategyType::BOLLINGER_BAND_MEAN_REVERSION,
            StrategyType::RSI_MEAN_REVERSION,
            StrategyType::MULTI_FACTOR_SELECTION,
            StrategyType::EARNINGS_SURPRISE,
            StrategyType::STATISTICAL_PAIR_TRADING,
            StrategyType::RISK_PARITY_ALLOCATION,
            StrategyType::MACHINE_LEARNING_SELECTION,
            StrategyType::ORDER_FLOW_IMBALANCE,
            StrategyType::VOLATILITY_SPREAD,
        };
    }

    /// @brief 策略类型 → 精确枚举名（落库 / 载荷格式，大写，严格区分大小写）
    [[nodiscard]] static constexpr std::string_view typeId(StrategyType type) noexcept
    {
        switch (type) {
        case StrategyType::DOUBLE_MOVING_AVERAGE:          return "DOUBLE_MOVING_AVERAGE";
        case StrategyType::TURTLE_BREAKOUT:                return "TURTLE_BREAKOUT";
        case StrategyType::BOLLINGER_BAND_MEAN_REVERSION:  return "BOLLINGER_BAND_MEAN_REVERSION";
        case StrategyType::RSI_MEAN_REVERSION:             return "RSI_MEAN_REVERSION";
        case StrategyType::MULTI_FACTOR_SELECTION:         return "MULTI_FACTOR_SELECTION";
        case StrategyType::EARNINGS_SURPRISE:              return "EARNINGS_SURPRISE";
        case StrategyType::STATISTICAL_PAIR_TRADING:       return "STATISTICAL_PAIR_TRADING";
        case StrategyType::RISK_PARITY_ALLOCATION:         return "RISK_PARITY_ALLOCATION";
        case StrategyType::MACHINE_LEARNING_SELECTION:     return "MACHINE_LEARNING_SELECTION";
        case StrategyType::ORDER_FLOW_IMBALANCE:           return "ORDER_FLOW_IMBALANCE";
        case StrategyType::VOLATILITY_SPREAD:              return "VOLATILITY_SPREAD";
        }
        return {};
    }

    /// @brief 枚举名 → 策略类型（严格精确匹配；不接受数字、旧键或模糊匹配，缺失直接 nullopt）
    [[nodiscard]] static constexpr std::optional<StrategyType> fromTypeId(std::string_view id) noexcept
    {
        for (const auto type : all()) {
            if (typeId(type) == id) {
                return type;
            }
        }
        return std::nullopt;
    }

    /// @brief 策略类型 → 行为类型（引擎分发与桥接层共用的唯一权威映射）
    [[nodiscard]] static constexpr StrategyBehaviorKind behaviorKindOf(StrategyType type) noexcept
    {
        switch (type) {
        case StrategyType::DOUBLE_MOVING_AVERAGE:          return StrategyBehaviorKind::TrendFollowing;
        case StrategyType::TURTLE_BREAKOUT:                return StrategyBehaviorKind::Momentum;
        case StrategyType::BOLLINGER_BAND_MEAN_REVERSION:  return StrategyBehaviorKind::MeanReversion;
        case StrategyType::RSI_MEAN_REVERSION:             return StrategyBehaviorKind::MeanReversion;
        case StrategyType::MULTI_FACTOR_SELECTION:         return StrategyBehaviorKind::MultiFactor;
        case StrategyType::EARNINGS_SURPRISE:              return StrategyBehaviorKind::EventDriven;
        case StrategyType::STATISTICAL_PAIR_TRADING:       return StrategyBehaviorKind::Arbitrage;
        case StrategyType::RISK_PARITY_ALLOCATION:         return StrategyBehaviorKind::MultiFactor;
        case StrategyType::MACHINE_LEARNING_SELECTION:     return StrategyBehaviorKind::MachineLearning;
        case StrategyType::ORDER_FLOW_IMBALANCE:           return StrategyBehaviorKind::HighFrequency;
        case StrategyType::VOLATILITY_SPREAD:              return StrategyBehaviorKind::Arbitrage;
        }
        return StrategyBehaviorKind::Custom;
    }

    /// @brief 策略类型 → 存储类型（显式 switch 逐名对应，禁止数值强转）
    [[nodiscard]] static constexpr domain::backtest::StrategyStoredType storedTypeOf(
        StrategyType type) noexcept
    {
        switch (type) {
        case StrategyType::DOUBLE_MOVING_AVERAGE:
            return domain::backtest::StrategyStoredType::DOUBLE_MOVING_AVERAGE;
        case StrategyType::TURTLE_BREAKOUT:
            return domain::backtest::StrategyStoredType::TURTLE_BREAKOUT;
        case StrategyType::BOLLINGER_BAND_MEAN_REVERSION:
            return domain::backtest::StrategyStoredType::BOLLINGER_BAND_MEAN_REVERSION;
        case StrategyType::RSI_MEAN_REVERSION:
            return domain::backtest::StrategyStoredType::RSI_MEAN_REVERSION;
        case StrategyType::MULTI_FACTOR_SELECTION:
            return domain::backtest::StrategyStoredType::MULTI_FACTOR_SELECTION;
        case StrategyType::EARNINGS_SURPRISE:
            return domain::backtest::StrategyStoredType::EARNINGS_SURPRISE;
        case StrategyType::STATISTICAL_PAIR_TRADING:
            return domain::backtest::StrategyStoredType::STATISTICAL_PAIR_TRADING;
        case StrategyType::RISK_PARITY_ALLOCATION:
            return domain::backtest::StrategyStoredType::RISK_PARITY_ALLOCATION;
        case StrategyType::MACHINE_LEARNING_SELECTION:
            return domain::backtest::StrategyStoredType::MACHINE_LEARNING_SELECTION;
        case StrategyType::ORDER_FLOW_IMBALANCE:
            return domain::backtest::StrategyStoredType::ORDER_FLOW_IMBALANCE;
        case StrategyType::VOLATILITY_SPREAD:
            return domain::backtest::StrategyStoredType::VOLATILITY_SPREAD;
        }
        return domain::backtest::StrategyStoredType::Unknown;
    }

    /// @brief 策略类型 → backtest 行为枚举（回测 identity 用；与行为枚举值对齐，见下方 static_assert）
    [[nodiscard]] static constexpr domain::backtest::StrategyBehaviorKind backtestBehaviorKindOf(
        StrategyType type) noexcept
    {
        return static_cast<domain::backtest::StrategyBehaviorKind>(
            static_cast<int>(behaviorKindOf(type)));
    }
};

// 编译期对齐守卫：两种行为枚举的每个成员值必须一致，backtestBehaviorKindOf 的转换才安全
static_assert(static_cast<int>(domain::backtest::StrategyBehaviorKind::TrendFollowing)
              == static_cast<int>(StrategyBehaviorKind::TrendFollowing));
static_assert(static_cast<int>(domain::backtest::StrategyBehaviorKind::MeanReversion)
              == static_cast<int>(StrategyBehaviorKind::MeanReversion));
static_assert(static_cast<int>(domain::backtest::StrategyBehaviorKind::Momentum)
              == static_cast<int>(StrategyBehaviorKind::Momentum));
static_assert(static_cast<int>(domain::backtest::StrategyBehaviorKind::Arbitrage)
              == static_cast<int>(StrategyBehaviorKind::Arbitrage));
static_assert(static_cast<int>(domain::backtest::StrategyBehaviorKind::MultiFactor)
              == static_cast<int>(StrategyBehaviorKind::MultiFactor));
static_assert(static_cast<int>(domain::backtest::StrategyBehaviorKind::MachineLearning)
              == static_cast<int>(StrategyBehaviorKind::MachineLearning));
static_assert(static_cast<int>(domain::backtest::StrategyBehaviorKind::EventDriven)
              == static_cast<int>(StrategyBehaviorKind::EventDriven));
static_assert(static_cast<int>(domain::backtest::StrategyBehaviorKind::HighFrequency)
              == static_cast<int>(StrategyBehaviorKind::HighFrequency));
static_assert(static_cast<int>(domain::backtest::StrategyBehaviorKind::Custom)
              == static_cast<int>(StrategyBehaviorKind::Custom));

} // namespace domain::strategies
