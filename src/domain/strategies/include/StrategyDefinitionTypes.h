#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../../factor/include/factor_enums.h"
#include "foundation/Utils/Uuid.h"

namespace domain::strategies {

using TechnicalPriceType = factor::TechnicalPriceType;
using SymbolId = std::uint32_t;
using StrategyUuid = foundation::utils::Uuid;
using FactorId = std::uint64_t;
using RuleId = std::uint64_t;
using StrategyId = std::uint64_t;

struct FactorSnapshot final {
    SymbolId symbolId{0};
    std::string factorId;  // instance_id 字符串
    double factorValue{0.0};
    std::int32_t industryBucket{0};

    [[nodiscard]] bool isValid() const noexcept
    {
        return symbolId != 0 && !factorId.empty();
    }
};

enum class StrategyBehaviorKind : std::uint8_t {
    TrendFollowing = 0,
    MeanReversion = 1,
    Momentum = 2,
    Arbitrage = 3,
    MultiFactor = 4,
    MachineLearning = 5,
    EventDriven = 6,
    HighFrequency = 7,
    Custom = 8
};

[[nodiscard]] inline bool isValidStrategyBehaviorKindIndex(int index) noexcept
{
    switch (static_cast<StrategyBehaviorKind>(index)) {
    case StrategyBehaviorKind::TrendFollowing:
    case StrategyBehaviorKind::MeanReversion:
    case StrategyBehaviorKind::Momentum:
    case StrategyBehaviorKind::Arbitrage:
    case StrategyBehaviorKind::MultiFactor:
    case StrategyBehaviorKind::MachineLearning:
    case StrategyBehaviorKind::EventDriven:
    case StrategyBehaviorKind::HighFrequency:
    case StrategyBehaviorKind::Custom:
        return true;
    }

    return false;
}

struct StrategyMetadata final {
    StrategyUuid uuid{foundation::utils::Uuid::null()};
    std::string name;
    std::string description;
    StrategyBehaviorKind behaviorKind{StrategyBehaviorKind::Custom};
    std::vector<std::string> factorIds;  // instance_id 字符串
    std::vector<RuleId> ruleIds;
    bool enabled{true};
};

struct FactorWeight final {
    std::string factorId;  // instance_id 字符串
    double weight{0.0};
};

enum class EventSourceKind : std::uint8_t {
    EARNINGS = 0,
    DIVIDEND = 1,
    GUIDANCE = 2
};

struct TradingSymbolPair final {
    SymbolId first{0};
    SymbolId second{0};
};

struct OptionChainFilterSpec final {
    int minDaysToExpiry{0};
    int maxDaysToExpiry{0};
    double minMoneyness{0.0};
    double maxMoneyness{0.0};
};

struct MarketBar final {
    SymbolId symbolId{0};
    double openPrice{0.0};
    double highPrice{0.0};
    double lowPrice{0.0};
    double closePrice{0.0};
    std::uint64_t volume{0};
    std::int32_t tradingDay{-1};

    [[nodiscard]] bool isValid() const
    {
        return symbolId != 0
            && openPrice > 0.0
            && highPrice > 0.0
            && lowPrice > 0.0
            && closePrice > 0.0
            && volume > 0
            && tradingDay >= 0;
    }
};

using MarketBarList = std::vector<MarketBar>;

enum class StrategyType : std::uint16_t {
    DOUBLE_MOVING_AVERAGE = 0,
    TURTLE_BREAKOUT = 1,
    BOLLINGER_BAND_MEAN_REVERSION = 2,
    RSI_MEAN_REVERSION = 3,
    MULTI_FACTOR_SELECTION = 4,
    EARNINGS_SURPRISE = 5,
    STATISTICAL_PAIR_TRADING = 6,
    RISK_PARITY_ALLOCATION = 7,
    MACHINE_LEARNING_SELECTION = 8,
    ORDER_FLOW_IMBALANCE = 9,
    VOLATILITY_SPREAD = 10
};

[[nodiscard]] inline bool isValidStrategyTypeIndex(int index) noexcept
{
    switch (static_cast<StrategyType>(index)) {
    case StrategyType::DOUBLE_MOVING_AVERAGE:
    case StrategyType::TURTLE_BREAKOUT:
    case StrategyType::BOLLINGER_BAND_MEAN_REVERSION:
    case StrategyType::RSI_MEAN_REVERSION:
    case StrategyType::MULTI_FACTOR_SELECTION:
    case StrategyType::EARNINGS_SURPRISE:
    case StrategyType::STATISTICAL_PAIR_TRADING:
    case StrategyType::RISK_PARITY_ALLOCATION:
    case StrategyType::MACHINE_LEARNING_SELECTION:
    case StrategyType::ORDER_FLOW_IMBALANCE:
    case StrategyType::VOLATILITY_SPREAD:
        return true;
    }

    return false;
}

enum class WeightScheme : std::uint8_t {
    EQUAL = 0,
    MARKET_CAP = 1,
    SIGNAL_STRENGTH = 2,
    RISK_PARITY = 3
};

enum class RebalanceFrequency : std::uint8_t {
    DAILY = 0,
    WEEKLY = 1,
    MONTHLY = 2,
    QUARTERLY = 3,
    YEARLY = 4
};

[[nodiscard]] inline bool isValidRebalanceFrequency(RebalanceFrequency frequency) noexcept
{
    switch (frequency) {
    case RebalanceFrequency::DAILY:
    case RebalanceFrequency::WEEKLY:
    case RebalanceFrequency::MONTHLY:
    case RebalanceFrequency::QUARTERLY:
    case RebalanceFrequency::YEARLY:
        return true;
    }

    return false;
}

[[nodiscard]] inline int rebalanceFrequencyStepInterval(RebalanceFrequency frequency) noexcept
{
    switch (frequency) {
    case RebalanceFrequency::DAILY:
        return 1;
    case RebalanceFrequency::WEEKLY:
        return 5;
    case RebalanceFrequency::MONTHLY:
        return 20;
    case RebalanceFrequency::QUARTERLY:
        return 60;
    case RebalanceFrequency::YEARLY:
        return 240;
    }

    return 0;
}

struct StrategyCommonConfig final {
    bool allowShort{false};
    int maxPositions{100};
    double maxWeightPerStock{0.1};
    double minWeightPerStock{0.0};
    WeightScheme weightScheme{WeightScheme::EQUAL};
    RebalanceFrequency rebalanceFrequency{RebalanceFrequency::DAILY};
    // 策略自定义参数 (从 parameters JSON 读取)
    int fastPeriod{5};
    int slowPeriod{20};
    int signalPeriod{14};
    int macdFast{12};
    int macdSlow{26};
    int macdSignal{9};
    int bbPeriod{20};
    double bbStdDev{2.0};
};

struct DoubleMovingAverageStrategySpec final {
    int fastPeriod{5};
    int slowPeriod{20};
    TechnicalPriceType priceField{TechnicalPriceType::CLOSE};
};

struct TurtleBreakoutStrategySpec final {
    int channelPeriod{20};
    double breakoutMultiplier{1.0};
    int atrPeriod{20};
};

struct BollingerBandMeanReversionStrategySpec final {
    int period{20};
    double standardDeviationMultiplier{2.0};
    double entryThreshold{1.0};
    double exitThreshold{0.2};
};

struct RsiMeanReversionStrategySpec final {
    int period{14};
    double oversoldLevel{30.0};
    double overboughtLevel{70.0};
};

struct MultiFactorSelectionStrategySpec final {
    std::vector<FactorWeight> factorWeights;
    int topN{50};
    bool industryNeutral{false};
};

struct EarningsSurpriseStrategySpec final {
    double surpriseThreshold{0.2};
    int holdDays{5};
    std::vector<EventSourceKind> eventSources;
};

struct StatisticalPairTradingStrategySpec final {
    TradingSymbolPair tradingPair;
    double hedgeRatio{1.0};
    int lookback{20};
    double entryZScore{2.0};
    double exitZScore{0.5};
};

struct RiskParityAllocationStrategySpec final {
    std::vector<SymbolId> assets;
    int volatilityLookback{60};
    double targetVolatility{0.0};
};

struct MachineLearningSelectionStrategySpec final {
    std::uint64_t modelId{0};
    std::vector<std::uint64_t> featureIds;
    int topN{50};
};

struct OrderFlowImbalanceStrategySpec final {
    int depthLevels{5};
    double imbalanceThreshold{0.3};
    int maxHoldSeconds{60};
};

struct VolatilitySpreadStrategySpec final {
    SymbolId underlying{0};
    OptionChainFilterSpec optionChainFilter;
    int historicalVolatilityWindow{20};
    double entrySpreadUpper{0.05};
    double entrySpreadLower{-0.05};
    bool deltaNeutral{true};
};

} // namespace domain::strategies
