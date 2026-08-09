// ParameterSpaceFactory.cpp — 策略类型 → 默认可调参数空间工厂
#include "../include/ParameterSpaceFactory.h"
#include "../include/ParameterSpace.h"

#include <algorithm>
#include <cstddef>

namespace domain::optimization {

std::vector<ParamRange> ParameterSpaceFactory::commonRanges() {
    return {
        ParamRange::intRange("maxPositions", 5, 200, 5),
        ParamRange::doubleRange("maxWeightPerStock", 0.02, 0.50, 0.02),
        ParamRange::doubleRange("minWeightPerStock", 0.0, 0.10, 0.01),
        ParamRange::enumRange("weightScheme", {
            {0, "EQUAL"},
            {1, "MARKET_CAP"},
            {2, "SIGNAL_STRENGTH"},
            {3, "RISK_PARITY"}
        }),
        ParamRange::enumRange("rebalanceFrequency", {
            {0, "DAILY"},
            {1, "WEEKLY"},
            {2, "MONTHLY"},
            {3, "QUARTERLY"},
            {4, "YEARLY"}
        }),
        ParamRange::boolRange("allowShort"),
        ParamRange::doubleRange("stopLossPercent", 3.0, 20.0, 1.0),
        ParamRange::doubleRange("takeProfitPercent", 10.0, 50.0, 5.0),
        ParamRange::intRange("minHoldDays", 0, 30, 1),
    };
}

std::vector<ParamRange> ParameterSpaceFactory::defaultRanges(
    domain::strategies::StrategyType strategyType) {
    using ST = domain::strategies::StrategyType;

    auto ranges = commonRanges();

    switch (strategyType) {
    case ST::DOUBLE_MOVING_AVERAGE: {
        ranges.push_back(ParamRange::intRange("fastPeriod", 3, 30, 1));
        ranges.push_back(ParamRange::intRange("slowPeriod", 10, 120, 5));
        ranges.push_back(ParamRange::enumRange("priceField", {
            {0, "OPEN"}, {1, "HIGH"}, {2, "LOW"}, {3, "CLOSE"}
        }));
        break;
    }
    case ST::TURTLE_BREAKOUT: {
        ranges.push_back(ParamRange::intRange("channelPeriod", 10, 60, 5));
        ranges.push_back(ParamRange::doubleRange("breakoutMultiplier", 0.5, 3.0, 0.5));
        ranges.push_back(ParamRange::intRange("atrPeriod", 10, 40, 5));
        break;
    }
    case ST::BOLLINGER_BAND_MEAN_REVERSION: {
        ranges.push_back(ParamRange::intRange("period", 10, 60, 5));
        ranges.push_back(ParamRange::doubleRange("standardDeviationMultiplier", 1.0, 4.0, 0.5));
        ranges.push_back(ParamRange::doubleRange("entryThreshold", 0.5, 3.0, 0.5));
        ranges.push_back(ParamRange::doubleRange("exitThreshold", 0.0, 1.0, 0.2));
        break;
    }
    case ST::RSI_MEAN_REVERSION: {
        ranges.push_back(ParamRange::intRange("signalPeriod", 5, 30, 1));
        ranges.push_back(ParamRange::doubleRange("oversoldLevel", 15.0, 40.0, 5.0));
        ranges.push_back(ParamRange::doubleRange("overboughtLevel", 60.0, 85.0, 5.0));
        break;
    }
    case ST::MULTI_FACTOR_SELECTION: {
        // MultiFactor 用更窄的 maxPositions 替代通用默认值
        ranges.erase(
            std::remove_if(ranges.begin(), ranges.end(),
                [](const ParamRange& r) { return r.name() == "maxPositions"; }),
            ranges.end());
        ranges.push_back(ParamRange::intRange("maxPositions", 10, 200, 5));

        ranges.push_back(ParamRange::intRange("topN", 10, 200, 5));
        ranges.push_back(ParamRange::doubleRange("minCompositeScore", 0.0, 3.0, 0.1));
        ranges.push_back(ParamRange::doubleRange("sellThreshold", 0.0, 1.0, 0.05));
        ranges.push_back(ParamRange::doubleRange("sellRankMultiplier", 1.0, 5.0, 0.5));
        ranges.push_back(ParamRange::boolRange("industryNeutral"));
        break;
    }
    case ST::STATISTICAL_PAIR_TRADING: {
        ranges.push_back(ParamRange::intRange("lookback", 10, 120, 10));
        ranges.push_back(ParamRange::doubleRange("entryZScore", 1.0, 3.5, 0.5));
        ranges.push_back(ParamRange::doubleRange("exitZScore", 0.0, 1.5, 0.5));
        break;
    }
    case ST::RISK_PARITY_ALLOCATION:
        // 资产配置类, 调仓频率/波动率窗口
        ranges.push_back(ParamRange::intRange("volatilityLookback", 20, 120, 10));
        ranges.push_back(ParamRange::doubleRange("targetVolatility", 0.05, 0.30, 0.05));
        break;
    case ST::MACHINE_LEARNING_SELECTION:
    case ST::EARNINGS_SURPRISE:
    case ST::ORDER_FLOW_IMBALANCE:
    case ST::VOLATILITY_SPREAD:
    default:
        // 依赖外部数据/模型/盘口，V1 仅通用参数
        break;
    }

    return ranges;
}

std::size_t ParameterSpaceFactory::estimateSearchSpace(
    const std::vector<ParamRange>& ranges) {
    ParameterSpace space(ranges);
    return space.totalCombinations();
}

bool ParameterSpaceFactory::wouldExplode(
    const std::vector<ParamRange>& ranges,
    std::size_t threshold) {
    return estimateSearchSpace(ranges) > threshold;
}

} // namespace domain::optimization
