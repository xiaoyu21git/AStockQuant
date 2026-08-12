#pragma once

#include <QObject>

#include "../../domain/strategies/include/StrategyDefinitionTypes.h"
#include "../../domain/types/ResolvedStrategyBehavior.h"

/// @brief QML 策略类型枚举契约 — C++ 枚举唯一事实源在 QML 侧的镜像
///
/// 设计约定:
/// - 每个成员值与 domain::strategies::StrategyType / StrategyBehaviorKind 逐值 static_assert 对齐
/// - 经 registerQmlTypes 注册为不可创建类型 "StrategyTypes",
///   QML 通过 Bridge.StrategyTypes.StrategyType.MachineLearningSelection 引用
/// - QML 侧禁止硬编码数字阶梯, 一切类型值必须来自本契约
/// - 本类只做编译期契约, 不含任何运行时映射逻辑 (映射统一走 domain::strategies::StrategyTypeRegistry)
class StrategyTypeContract : public QObject {
    Q_OBJECT
public:
    /// @brief 策略类型 (0-10, 与 domain::strategies::StrategyType 逐值一致)
    enum class StrategyType {
        DoubleMovingAverage = 0,
        TurtleBreakout = 1,
        BollingerBandMeanReversion = 2,
        RsiMeanReversion = 3,
        MultiFactorSelection = 4,
        EarningsSurprise = 5,
        StatisticalPairTrading = 6,
        RiskParityAllocation = 7,
        MachineLearningSelection = 8,
        OrderFlowImbalance = 9,
        VolatilitySpread = 10,
    };
    Q_ENUM(StrategyType)

    /// @brief 行为类型 (0-8, 与 domain::strategies::StrategyBehaviorKind 逐值一致; 展示用, 一律由类型推导)
    enum class StrategyBehaviorKind {
        TrendFollowing = 0,
        MeanReversion = 1,
        Momentum = 2,
        Arbitrage = 3,
        MultiFactor = 4,
        MachineLearning = 5,
        EventDriven = 6,
        HighFrequency = 7,
        Custom = 8,
    };
    Q_ENUM(StrategyBehaviorKind)

    explicit StrategyTypeContract(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
};

// ── 编译期对齐守卫: QML 契约枚举与域层枚举每个成员值必须一致 ──
using QtStrategyType = StrategyTypeContract::StrategyType;
using QtStrategyBk = StrategyTypeContract::StrategyBehaviorKind;
using DomainStrategyType = domain::strategies::StrategyType;
using DomainStrategyBk = domain::strategies::StrategyBehaviorKind;

static_assert(static_cast<int>(QtStrategyType::DoubleMovingAverage)
              == static_cast<int>(DomainStrategyType::DOUBLE_MOVING_AVERAGE));
static_assert(static_cast<int>(QtStrategyType::TurtleBreakout)
              == static_cast<int>(DomainStrategyType::TURTLE_BREAKOUT));
static_assert(static_cast<int>(QtStrategyType::BollingerBandMeanReversion)
              == static_cast<int>(DomainStrategyType::BOLLINGER_BAND_MEAN_REVERSION));
static_assert(static_cast<int>(QtStrategyType::RsiMeanReversion)
              == static_cast<int>(DomainStrategyType::RSI_MEAN_REVERSION));
static_assert(static_cast<int>(QtStrategyType::MultiFactorSelection)
              == static_cast<int>(DomainStrategyType::MULTI_FACTOR_SELECTION));
static_assert(static_cast<int>(QtStrategyType::EarningsSurprise)
              == static_cast<int>(DomainStrategyType::EARNINGS_SURPRISE));
static_assert(static_cast<int>(QtStrategyType::StatisticalPairTrading)
              == static_cast<int>(DomainStrategyType::STATISTICAL_PAIR_TRADING));
static_assert(static_cast<int>(QtStrategyType::RiskParityAllocation)
              == static_cast<int>(DomainStrategyType::RISK_PARITY_ALLOCATION));
static_assert(static_cast<int>(QtStrategyType::MachineLearningSelection)
              == static_cast<int>(DomainStrategyType::MACHINE_LEARNING_SELECTION));
static_assert(static_cast<int>(QtStrategyType::OrderFlowImbalance)
              == static_cast<int>(DomainStrategyType::ORDER_FLOW_IMBALANCE));
static_assert(static_cast<int>(QtStrategyType::VolatilitySpread)
              == static_cast<int>(DomainStrategyType::VOLATILITY_SPREAD));

static_assert(static_cast<int>(QtStrategyBk::TrendFollowing)
              == static_cast<int>(DomainStrategyBk::TrendFollowing));
static_assert(static_cast<int>(QtStrategyBk::MeanReversion)
              == static_cast<int>(DomainStrategyBk::MeanReversion));
static_assert(static_cast<int>(QtStrategyBk::Momentum)
              == static_cast<int>(DomainStrategyBk::Momentum));
static_assert(static_cast<int>(QtStrategyBk::Arbitrage)
              == static_cast<int>(DomainStrategyBk::Arbitrage));
static_assert(static_cast<int>(QtStrategyBk::MultiFactor)
              == static_cast<int>(DomainStrategyBk::MultiFactor));
static_assert(static_cast<int>(QtStrategyBk::MachineLearning)
              == static_cast<int>(DomainStrategyBk::MachineLearning));
static_assert(static_cast<int>(QtStrategyBk::EventDriven)
              == static_cast<int>(DomainStrategyBk::EventDriven));
static_assert(static_cast<int>(QtStrategyBk::HighFrequency)
              == static_cast<int>(DomainStrategyBk::HighFrequency));
static_assert(static_cast<int>(QtStrategyBk::Custom)
              == static_cast<int>(DomainStrategyBk::Custom));
