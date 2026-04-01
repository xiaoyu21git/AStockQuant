#include "BacktestEngine.h"

#include <foundation.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

enum class TradingSignal {
    Hold,
    Buy,
    Sell
};

struct PositionState {
    double quantity{0.0};
    double entryPrice{0.0};
    foundation::Timestamp entryTime;

    bool hasPosition() const {
        return quantity > 0.0 && entryPrice > 0.0;
    }
};

struct SymbolRuntimeState {
    std::vector<double> closes;
    PositionState position;
};

struct BacktestRuntimeState {
    double cash{0.0};
    std::unordered_map<std::string, SymbolRuntimeState> symbolStates;
    std::unordered_map<std::string, double> latestPrices;
    std::unordered_map<std::string, foundation::Timestamp> latestTimestamps;
};

struct StrategyProfile {
    std::string subtype;
    double positionSizeRatio{1.0};
    int fastPeriod{10};
    int slowPeriod{30};
    int bollPeriod{20};
    double bollStd{2.0};
    double reversionThreshold{0.5};
    int momentumPeriod{60};
    double spreadThreshold{0.02};
    double entryZScore{2.0};
    double exitZScore{0.5};
    double stopLossRate{0.05};
    double takeProfitRate{0.15};
};

double clampPositive(double value, double fallback) {
    return value > 0.0 ? value : fallback;
}

double getDoubleParam(const std::map<std::string, double>& params,
                     const std::string& primaryKey,
                     const std::string& secondaryKey,
                     double fallback) {
    const auto primary = params.find(primaryKey);
    if (primary != params.end()) {
        return primary->second;
    }
    const auto secondary = params.find(secondaryKey);
    if (secondary != params.end()) {
        return secondary->second;
    }
    return fallback;
}

std::string getStringOption(const std::map<std::string, std::string>& options,
                            const std::string& key) {
    const auto it = options.find(key);
    return it == options.end() ? std::string() : it->second;
}

double calculateMean(const std::vector<double>& values, std::size_t begin, std::size_t end) {
    if (begin >= end || end > values.size()) {
        return 0.0;
    }

    double sum = 0.0;
    for (std::size_t index = begin; index < end; ++index) {
        sum += values[index];
    }
    return sum / static_cast<double>(end - begin);
}

double calculateStdDev(const std::vector<double>& values, std::size_t begin, std::size_t end, double mean) {
    if (begin >= end || end - begin < 2 || end > values.size()) {
        return 0.0;
    }

    double variance = 0.0;
    for (std::size_t index = begin; index < end; ++index) {
        const double diff = values[index] - mean;
        variance += diff * diff;
    }
    variance /= static_cast<double>(end - begin - 1);
    return std::sqrt((std::max)(variance, 0.0));
}

StrategyProfile buildStrategyProfile(const std::string& strategyName,
                                     double maxPositionRatio,
                                     const std::map<std::string, double>& strategyParams,
                                     const std::map<std::string, std::string>& strategyOptions) {
    StrategyProfile profile;
    profile.fastPeriod = (std::max)(2, static_cast<int>(std::round(getDoubleParam(strategyParams, "fast_period", "fastPeriod", 10.0))));
    profile.slowPeriod = (std::max)(profile.fastPeriod + 1, static_cast<int>(std::round(getDoubleParam(strategyParams, "slow_period", "slowPeriod", 30.0))));
    profile.bollPeriod = (std::max)(5, static_cast<int>(std::round(getDoubleParam(strategyParams, "boll_period", "bollPeriod", 20.0))));
    profile.bollStd = clampPositive(getDoubleParam(strategyParams, "boll_std", "bollStd", 2.0), 2.0);
    profile.reversionThreshold = clampPositive(getDoubleParam(strategyParams, "reversion_threshold", "reversionThreshold", 0.5), 0.5);
    profile.momentumPeriod = (std::max)(5, static_cast<int>(std::round(getDoubleParam(strategyParams, "momentum_period", "momentumPeriod", 60.0))));
    profile.spreadThreshold = clampPositive(getDoubleParam(strategyParams, "spread_threshold", "spreadThreshold", 0.02), 0.02);
    profile.entryZScore = clampPositive(getDoubleParam(strategyParams, "entry_z_score", "entryZScore", 2.0), 2.0);
    profile.exitZScore = clampPositive(getDoubleParam(strategyParams, "exit_z_score", "exitZScore", 0.5), 0.5);
    profile.stopLossRate = clampPositive(getDoubleParam(strategyParams, "stop_loss", "stopLossPercent", 0.05), 0.05);
    profile.takeProfitRate = clampPositive(getDoubleParam(strategyParams, "take_profit", "takeProfitPercent", 0.15), 0.15);

    const double configuredPosition = getDoubleParam(strategyParams, "position_size", "positionSize", maxPositionRatio);
    profile.positionSizeRatio = configuredPosition > 1.0 ? configuredPosition / 100.0 : configuredPosition;
    if (profile.positionSizeRatio <= 0.0) {
        profile.positionSizeRatio = maxPositionRatio;
    }
    profile.positionSizeRatio = (std::min)((std::max)(profile.positionSizeRatio, 0.01), (std::max)(maxPositionRatio, 0.01));

    profile.subtype = getStringOption(strategyOptions, "strategy_subtype");
    if (profile.subtype.empty()) {
        profile.subtype = getStringOption(strategyOptions, "sub_type");
    }
    if (profile.subtype.empty()) {
        profile.subtype = getStringOption(strategyOptions, "strategy_type");
    }
    if (profile.subtype.empty()) {
        profile.subtype = strategyName;
    }

    std::transform(profile.subtype.begin(), profile.subtype.end(), profile.subtype.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return profile;
}

TradingSignal evaluateSignal(const StrategyProfile& profile,
                             const std::vector<double>& closes,
                             const PositionState& position) {
    if (closes.empty()) {
        return TradingSignal::Hold;
    }

    const double currentPrice = closes.back();
    if (position.hasPosition()) {
        const double pnlRatio = currentPrice / position.entryPrice - 1.0;
        if (pnlRatio <= -profile.stopLossRate || pnlRatio >= profile.takeProfitRate) {
            return TradingSignal::Sell;
        }
    }

    const bool isMeanReversion = profile.subtype.find("mean") != std::string::npos || profile.subtype.find("reversion") != std::string::npos;
    const bool isMomentum = profile.subtype.find("alpha") != std::string::npos || profile.subtype.find("momentum") != std::string::npos;
    const bool isArbitrage = profile.subtype.find("arbitrage") != std::string::npos;

    if (isMeanReversion) {
        if (closes.size() < static_cast<std::size_t>(profile.bollPeriod)) {
            return TradingSignal::Hold;
        }

        const std::size_t begin = closes.size() - static_cast<std::size_t>(profile.bollPeriod);
        const double mean = calculateMean(closes, begin, closes.size());
        const double stdDev = calculateStdDev(closes, begin, closes.size(), mean);
        if (stdDev <= std::numeric_limits<double>::epsilon()) {
            return TradingSignal::Hold;
        }

        const double lowerBand = mean - profile.bollStd * stdDev;
        const double exitLevel = mean - profile.reversionThreshold * stdDev;
        if (!position.hasPosition() && currentPrice <= lowerBand) {
            return TradingSignal::Buy;
        }
        if (position.hasPosition() && currentPrice >= exitLevel) {
            return TradingSignal::Sell;
        }
        return TradingSignal::Hold;
    }

    if (isArbitrage) {
        if (closes.size() < static_cast<std::size_t>(profile.bollPeriod)) {
            return TradingSignal::Hold;
        }

        const std::size_t begin = closes.size() - static_cast<std::size_t>(profile.bollPeriod);
        const double mean = calculateMean(closes, begin, closes.size());
        const double stdDev = calculateStdDev(closes, begin, closes.size(), mean);
        if (stdDev <= std::numeric_limits<double>::epsilon()) {
            return TradingSignal::Hold;
        }

        const double zScore = (currentPrice - mean) / stdDev;
        if (!position.hasPosition() && zScore <= -profile.entryZScore) {
            return TradingSignal::Buy;
        }
        if (position.hasPosition() && zScore >= -profile.exitZScore) {
            return TradingSignal::Sell;
        }
        return TradingSignal::Hold;
    }

    if (isMomentum) {
        if (closes.size() <= static_cast<std::size_t>(profile.momentumPeriod)) {
            return TradingSignal::Hold;
        }

        const double basePrice = closes[closes.size() - static_cast<std::size_t>(profile.momentumPeriod) - 1];
        if (basePrice <= 0.0) {
            return TradingSignal::Hold;
        }

        const double momentum = currentPrice / basePrice - 1.0;
        if (!position.hasPosition() && momentum >= profile.spreadThreshold) {
            return TradingSignal::Buy;
        }
        if (position.hasPosition() && momentum <= 0.0) {
            return TradingSignal::Sell;
        }
        return TradingSignal::Hold;
    }

    if (closes.size() < static_cast<std::size_t>(profile.slowPeriod + 1)) {
        return TradingSignal::Hold;
    }

    const std::size_t size = closes.size();
    const double currentFast = calculateMean(closes, size - static_cast<std::size_t>(profile.fastPeriod), size);
    const double currentSlow = calculateMean(closes, size - static_cast<std::size_t>(profile.slowPeriod), size);
    const double previousFast = calculateMean(closes, size - static_cast<std::size_t>(profile.fastPeriod) - 1, size - 1);
    const double previousSlow = calculateMean(closes, size - static_cast<std::size_t>(profile.slowPeriod) - 1, size - 1);

    if (!position.hasPosition() && previousFast <= previousSlow && currentFast > currentSlow) {
        return TradingSignal::Buy;
    }
    if (position.hasPosition() && previousFast >= previousSlow && currentFast < currentSlow) {
        return TradingSignal::Sell;
    }
    return TradingSignal::Hold;
}

double calculatePortfolioEquity(const BacktestRuntimeState& state) {
    double equity = state.cash;
    for (const auto& entry : state.symbolStates) {
        if (!entry.second.position.hasPosition()) {
            continue;
        }

        const auto priceIt = state.latestPrices.find(entry.first);
        if (priceIt == state.latestPrices.end()) {
            continue;
        }
        equity += entry.second.position.quantity * priceIt->second;
    }
    return equity;
}

void closePosition(engine::BacktestResult& result,
                   const std::string& symbol,
                   double price,
                   foundation::Timestamp timestamp,
                   double commissionRate,
                   double slippageRate,
                   SymbolRuntimeState& symbolState,
                   BacktestRuntimeState& runtimeState,
                   const std::string& note) {
    PositionState& position = symbolState.position;
    if (!position.hasPosition() || price <= 0.0) {
        return;
    }

    const double exitPrice = slippageRate > 0.0 ? price * (1.0 - slippageRate) : price;
    const double entryCommission = commissionRate > 0.0 ? position.entryPrice * position.quantity * commissionRate : 0.0;
    const double exitCommission = commissionRate > 0.0 ? exitPrice * position.quantity * commissionRate : 0.0;
    const double totalCommission = entryCommission + exitCommission;
    const double grossProfit = (exitPrice - position.entryPrice) * position.quantity;
    const double profit = grossProfit - totalCommission;

    engine::BacktestResult::TradeRecord record{};
    record.trade_id = foundation::Uuid{};
    record.entry_time = position.entryTime;
    record.exit_time = timestamp;
    record.symbol = symbol;
    record.direction = "SELL";
    record.entry_price = position.entryPrice;
    record.exit_price = exitPrice;
    record.quantity = position.quantity;
    record.commission = totalCommission;
    record.profit = profit;
    record.profit_pct = position.entryPrice > 0.0 ? profit / (position.entryPrice * position.quantity) : 0.0;
    record.notes = note;
    result.add_trade_record(record);

    runtimeState.cash += position.quantity * exitPrice - exitCommission;
    position = PositionState{};
}

void processBar(engine::BacktestResult& result,
                const domain::model::Bar& bar,
                const StrategyProfile& profile,
                double max_position_ratio,
                double commission_rate,
                double slippage_rate,
                double min_volume,
                BacktestRuntimeState& state) {
    if (bar.close <= 0.0) {
        return;
    }

    foundation::Timestamp timestamp = foundation::Timestamp::from_seconds(bar.time / 1000);
    SymbolRuntimeState& symbolState = state.symbolStates[bar.symbol];
    symbolState.closes.push_back(bar.close);
    state.latestPrices[bar.symbol] = bar.close;
    state.latestTimestamps[bar.symbol] = timestamp;

    if (min_volume > 0.0 && bar.volume > 0.0 && bar.volume < min_volume) {
        result.update_equity_curve(timestamp, calculatePortfolioEquity(state));
        return;
    }

    const TradingSignal signal = evaluateSignal(profile, symbolState.closes, symbolState.position);
    if (signal == TradingSignal::Buy && !symbolState.position.hasPosition()) {
        const double tradePrice = slippage_rate > 0.0 ? bar.close * (1.0 + slippage_rate) : bar.close;
        const double currentEquity = calculatePortfolioEquity(state);
        const double allocationRatio = (std::min)((std::max)(profile.positionSizeRatio, 0.01), (std::max)(max_position_ratio, 0.01));
        const double targetCash = currentEquity * allocationRatio;
        const double investCash = (std::min)(state.cash, targetCash);
        const double rawQuantity = tradePrice > 0.0 ? investCash / tradePrice : 0.0;
        const double quantity = std::floor(rawQuantity / 100.0) * 100.0;

        if (quantity > 0.0) {
            const double entryCommission = commission_rate > 0.0 ? tradePrice * quantity * commission_rate : 0.0;
            const double totalCost = tradePrice * quantity + entryCommission;
            if (totalCost <= state.cash) {
                state.cash -= totalCost;
                symbolState.position.quantity = quantity;
                symbolState.position.entryPrice = tradePrice;
                symbolState.position.entryTime = timestamp;
            }
        }
    } else if (signal == TradingSignal::Sell && symbolState.position.hasPosition()) {
        closePosition(result,
                      bar.symbol,
                      bar.close,
                      timestamp,
                      commission_rate,
                      slippage_rate,
                      symbolState,
                      state,
                      "signal exit");
    }

    result.update_equity_curve(timestamp, calculatePortfolioEquity(state));
}

void finalizeOpenPositions(engine::BacktestResult& result,
                           double commissionRate,
                           double slippageRate,
                           BacktestRuntimeState& state) {
    for (auto& entry : state.symbolStates) {
        const auto priceIt = state.latestPrices.find(entry.first);
        const auto timeIt = state.latestTimestamps.find(entry.first);
        if (priceIt == state.latestPrices.end() || timeIt == state.latestTimestamps.end()) {
            continue;
        }

        closePosition(result,
                      entry.first,
                      priceIt->second,
                      timeIt->second,
                      commissionRate,
                      slippageRate,
                      entry.second,
                      state,
                      "final close");
    }

    if (!state.latestTimestamps.empty()) {
        auto latest = state.latestTimestamps.begin()->second;
        for (const auto& item : state.latestTimestamps) {
            if (item.second.to_milliseconds() > latest.to_milliseconds()) {
                latest = item.second;
            }
        }
        result.update_equity_curve(latest, calculatePortfolioEquity(state));
    }
}

} // namespace

namespace engine {

BacktestResult BacktestEngine::run(
    const std::vector<domain::model::Bar>& bars,
    double initial_capital,
    const std::string& strategy_name,
    double max_position_ratio,
    double commission_rate,
    double slippage_rate,
    double min_volume)
{
    return run(bars,
               initial_capital,
               strategy_name,
               max_position_ratio,
               commission_rate,
               slippage_rate,
               min_volume,
               {},
               {});
}

BacktestResult BacktestEngine::run(
    const std::vector<domain::model::Bar>& bars,
    double initial_capital,
    const std::string& strategy_name,
    double max_position_ratio,
    double commission_rate,
    double slippage_rate,
    double min_volume,
    const std::map<std::string, double>& strategy_params,
    const std::map<std::string, std::string>& strategy_options)
{
    BacktestResult result;
    if (bars.empty()) {
        return result;
    }

    BacktestRuntimeState state;
    state.cash = initial_capital;
    const StrategyProfile profile = buildStrategyProfile(strategy_name, max_position_ratio, strategy_params, strategy_options);

    for (std::size_t i = 0; i < bars.size(); ++i) {
        processBar(result,
                   bars[i],
                   profile,
                   max_position_ratio,
                   commission_rate,
                   slippage_rate,
                   min_volume,
                   state);
    }

    finalizeOpenPositions(result, commission_rate, slippage_rate, state);
    result.calculate_all_metrics();
    return result;
}

BacktestResult BacktestEngine::run(
    const std::vector<std::vector<domain::model::Bar>>& barSeries,
    double initial_capital,
    const std::string& strategy_name,
    double max_position_ratio,
    double commission_rate,
    double slippage_rate,
    double min_volume)
{
    return run(barSeries,
               initial_capital,
               strategy_name,
               max_position_ratio,
               commission_rate,
               slippage_rate,
               min_volume,
               {},
               {});
}

BacktestResult BacktestEngine::run(
    const std::vector<std::vector<domain::model::Bar>>& barSeries,
    double initial_capital,
    const std::string& strategy_name,
    double max_position_ratio,
    double commission_rate,
    double slippage_rate,
    double min_volume,
    const std::map<std::string, double>& strategy_params,
    const std::map<std::string, std::string>& strategy_options)
{
    BacktestResult result;

    struct Cursor {
        std::size_t seriesIndex;
        std::size_t barIndex;
    };

    struct CursorCompare {
        const std::vector<std::vector<domain::model::Bar>>* series;

        bool operator()(const Cursor& left, const Cursor& right) const {
            const auto& leftBar = (*series)[left.seriesIndex][left.barIndex];
            const auto& rightBar = (*series)[right.seriesIndex][right.barIndex];
            if (leftBar.time == rightBar.time) {
                return leftBar.symbol > rightBar.symbol;
            }
            return leftBar.time > rightBar.time;
        }
    };

    std::priority_queue<Cursor, std::vector<Cursor>, CursorCompare> heap{CursorCompare{&barSeries}};
    for (std::size_t seriesIndex = 0; seriesIndex < barSeries.size(); ++seriesIndex) {
        if (!barSeries[seriesIndex].empty()) {
            heap.push(Cursor{seriesIndex, 0});
        }
    }

    if (heap.empty()) {
        return result;
    }

    BacktestRuntimeState state;
    state.cash = initial_capital;
    const StrategyProfile profile = buildStrategyProfile(strategy_name, max_position_ratio, strategy_params, strategy_options);

    while (!heap.empty()) {
        Cursor current = heap.top();
        heap.pop();

        const auto& bar = barSeries[current.seriesIndex][current.barIndex];
        processBar(result,
                   bar,
                   profile,
                   max_position_ratio,
                   commission_rate,
                   slippage_rate,
                   min_volume,
                   state);

        const std::size_t nextBarIndex = current.barIndex + 1;
        if (nextBarIndex < barSeries[current.seriesIndex].size()) {
            heap.push(Cursor{current.seriesIndex, nextBarIndex});
        }
    }

    finalizeOpenPositions(result, commission_rate, slippage_rate, state);
    result.calculate_all_metrics();
    return result;
}

} // namespace engine
