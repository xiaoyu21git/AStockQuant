#include "StrategyBacktestService.h"
#include "DatabaseStockDataProvider.h"
#include "DatabaseFactorDataProvider.h"
#include "../include/BacktestEngine.h"
#include "../../../foundation/include/foundation.h"
#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <set>
#include <unordered_map>
#include <cmath>
#include <QDebug>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

struct PortfolioFactorAllocation {
    std::string factorId;
    double weight{0.0};
};

struct PortfolioPositionState {
    double quantity{0.0};
    double entryPrice{0.0};
    foundation::Timestamp entryTime;

    bool hasPosition() const {
        return quantity > 0.0 && entryPrice > 0.0;
    }
};

struct PortfolioRuntimeState {
    double cash{0.0};
    std::unordered_map<std::string, PortfolioPositionState> positions;
    std::unordered_map<std::string, double> latestPrices;
    std::unordered_map<std::string, foundation::Timestamp> latestTimestamps;
};

bool isPortfolioStrategyConfig(const domain::backtest::StrategyBacktestConfig& config)
{
    auto hasOptionValue = [&config](const std::string& key, const std::string& expectedUpper, const std::string& expectedLower) {
        const auto it = config.strategyOptions.find(key);
        if (it == config.strategyOptions.end()) {
            return false;
        }

        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value == expectedLower || value == expectedUpper;
    };

    return hasOptionValue("strategy_type", "portfolio", "portfolio")
        || hasOptionValue("strategy_subtype", "portfolio_builder", "portfolio_builder")
        || hasOptionValue("sub_type", "portfolio_builder", "portfolio_builder")
        || hasOptionValue("portfolio_source", "portfolio_builder", "portfolio_builder");
}

double normalizedRatio(double value, double fallback)
{
    if (!std::isfinite(value) || value <= 0.0) {
        return fallback;
    }
    return value > 1.0 ? value / 100.0 : value;
}

int integerStrategyParam(const std::map<std::string, double>& params,
                         const std::string& key,
                         int fallback)
{
    const auto it = params.find(key);
    if (it == params.end() || !std::isfinite(it->second)) {
        return fallback;
    }
    return (std::max)(1, static_cast<int>(std::round(it->second)));
}

double strategyRatioParam(const std::map<std::string, double>& params,
                          std::initializer_list<const char*> keys,
                          double fallback)
{
    for (const char* key : keys) {
        const auto it = params.find(key);
        if (it == params.end() || !std::isfinite(it->second) || it->second <= 0.0) {
            continue;
        }
        return normalizedRatio(it->second, fallback);
    }

    return fallback;
}

double optionalStrategyRatioParam(const std::map<std::string, double>& params,
                                  std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const auto it = params.find(key);
        if (it == params.end() || !std::isfinite(it->second) || it->second <= 0.0) {
            continue;
        }
        return normalizedRatio(it->second, 0.0);
    }

    return 0.0;
}

std::vector<std::string> splitCommaSeparated(const std::string& rawText)
{
    std::vector<std::string> values;
    std::stringstream stream(rawText);
    std::string item;
    while (std::getline(stream, item, ',')) {
        item.erase(item.begin(), std::find_if(item.begin(), item.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        item.erase(std::find_if(item.rbegin(), item.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), item.end());
        if (!item.empty()) {
            values.push_back(item);
        }
    }
    return values;
}

std::vector<PortfolioFactorAllocation> parsePortfolioAllocations(
    const domain::backtest::StrategyBacktestConfig& config)
{
    std::vector<PortfolioFactorAllocation> allocations;

    const auto rawAllocationsIt = config.strategyOptions.find("portfolio_allocations_json");
    if (rawAllocationsIt != config.strategyOptions.end() && !rawAllocationsIt->second.empty()) {
        json parsed = json::parse(rawAllocationsIt->second, nullptr, false);
        if (parsed.is_array()) {
            for (const auto& item : parsed) {
                if (!item.is_object()) {
                    continue;
                }

                const std::string factorId = item.value("factor_id", item.value("factorId", std::string()));
                const double rawWeight = item.value("weight", 0.0);
                const double weight = normalizedRatio(rawWeight, 0.0);
                if (!factorId.empty() && weight > 0.0) {
                    allocations.push_back({factorId, weight});
                }
            }
        }
    }

    if (allocations.empty()) {
        const auto factorIdsIt = config.strategyOptions.find("portfolio_factor_ids");
        if (factorIdsIt != config.strategyOptions.end()) {
            const auto factorIds = splitCommaSeparated(factorIdsIt->second);
            if (!factorIds.empty()) {
                const double equalWeight = 1.0 / static_cast<double>(factorIds.size());
                for (const auto& factorId : factorIds) {
                    allocations.push_back({factorId, equalWeight});
                }
            }
        }
    }

    double totalWeight = 0.0;
    for (const auto& allocation : allocations) {
        totalWeight += allocation.weight;
    }

    if (totalWeight > 0.0) {
        for (auto& allocation : allocations) {
            allocation.weight /= totalWeight;
        }
    }

    return allocations;
}

std::string barDateKey(const domain::model::Bar& bar)
{
    return foundation::Timestamp::from_seconds(bar.time / 1000).to_string("%Y-%m-%d");
}

foundation::Timestamp barTimestamp(const domain::model::Bar& bar)
{
    return foundation::Timestamp::from_seconds(bar.time / 1000);
}

double calculatePortfolioEquity(const PortfolioRuntimeState& state)
{
    double equity = state.cash;
    for (const auto& entry : state.positions) {
        if (!entry.second.hasPosition()) {
            continue;
        }

        const auto priceIt = state.latestPrices.find(entry.first);
        if (priceIt == state.latestPrices.end()) {
            continue;
        }
        equity += entry.second.quantity * priceIt->second;
    }
    return equity;
}

void closePortfolioPosition(engine::BacktestResult& result,
                            const std::string& symbol,
                            double price,
                            foundation::Timestamp timestamp,
                            double commissionRate,
                            double slippageRate,
                            PortfolioRuntimeState& runtimeState,
                            const std::string& note)
{
    auto positionIt = runtimeState.positions.find(symbol);
    if (positionIt == runtimeState.positions.end() || !positionIt->second.hasPosition() || price <= 0.0) {
        return;
    }

    PortfolioPositionState& position = positionIt->second;
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
    position = PortfolioPositionState{};
}

void reducePortfolioPosition(engine::BacktestResult& result,
                             const std::string& symbol,
                             double price,
                             foundation::Timestamp timestamp,
                             double commissionRate,
                             double slippageRate,
                             PortfolioRuntimeState& runtimeState,
                             double reduceRatio,
                             const std::string& note)
{
    auto positionIt = runtimeState.positions.find(symbol);
    if (positionIt == runtimeState.positions.end() || !positionIt->second.hasPosition() || price <= 0.0) {
        return;
    }

    PortfolioPositionState& position = positionIt->second;
    const double boundedReduceRatio = (std::min)(1.0, (std::max)(0.0, reduceRatio));
    if (boundedReduceRatio <= 0.0) {
        return;
    }

    double quantityToClose = std::floor((position.quantity * boundedReduceRatio) / 100.0) * 100.0;
    if (quantityToClose <= 0.0 || quantityToClose >= position.quantity) {
        closePortfolioPosition(result, symbol, price, timestamp, commissionRate, slippageRate, runtimeState, note);
        return;
    }

    const double exitPrice = slippageRate > 0.0 ? price * (1.0 - slippageRate) : price;
    const double entryCommission = commissionRate > 0.0 ? position.entryPrice * quantityToClose * commissionRate : 0.0;
    const double exitCommission = commissionRate > 0.0 ? exitPrice * quantityToClose * commissionRate : 0.0;
    const double totalCommission = entryCommission + exitCommission;
    const double grossProfit = (exitPrice - position.entryPrice) * quantityToClose;
    const double profit = grossProfit - totalCommission;

    engine::BacktestResult::TradeRecord record{};
    record.trade_id = foundation::Uuid{};
    record.entry_time = position.entryTime;
    record.exit_time = timestamp;
    record.symbol = symbol;
    record.direction = "SELL";
    record.entry_price = position.entryPrice;
    record.exit_price = exitPrice;
    record.quantity = quantityToClose;
    record.commission = totalCommission;
    record.profit = profit;
    record.profit_pct = position.entryPrice > 0.0 ? profit / (position.entryPrice * quantityToClose) : 0.0;
    record.notes = note;
    result.add_trade_record(record);

    runtimeState.cash += quantityToClose * exitPrice - exitCommission;
    position.quantity -= quantityToClose;
    if (position.quantity <= 0.0) {
        position = PortfolioPositionState{};
    }
}

bool shouldTriggerPortfolioRiskExit(const PortfolioPositionState& position,
                                    double currentPrice,
                                    double stopLossRate,
                                    double takeProfitRate,
                                    std::string& exitNote)
{
    if (!position.hasPosition() || currentPrice <= 0.0 || position.entryPrice <= 0.0) {
        return false;
    }

    const double pnlRatio = currentPrice / position.entryPrice - 1.0;
    if (stopLossRate > 0.0 && pnlRatio <= -stopLossRate) {
        exitNote = "stop loss exit";
        return true;
    }

    if (takeProfitRate > 0.0 && pnlRatio >= takeProfitRate) {
        exitNote = "take profit exit";
        return true;
    }

    return false;
}

std::vector<std::string> selectPortfolioSymbols(
    const std::string& tradeDate,
    const std::vector<const domain::model::Bar*>& dailyBars,
    const std::vector<PortfolioFactorAllocation>& allocations,
    const std::map<std::string, std::map<std::string, std::map<std::string, double>>>& factorSeriesByFactor,
    int topN)
{
    struct ScoreState {
        double score{0.0};
        int contributionCount{0};
    };

    std::set<std::string> tradableSymbols;
    for (const auto* bar : dailyBars) {
        if (bar && bar->close > 0.0) {
            tradableSymbols.insert(bar->symbol);
        }
    }

    if (tradableSymbols.empty()) {
        return {};
    }

    std::map<std::string, ScoreState> scores;
    for (const auto& allocation : allocations) {
        const auto factorIt = factorSeriesByFactor.find(allocation.factorId);
        if (factorIt == factorSeriesByFactor.end()) {
            continue;
        }

        const auto& factorSeries = factorIt->second;
        auto snapshotIt = factorSeries.upper_bound(tradeDate);
        if (snapshotIt == factorSeries.begin()) {
            continue;
        }
        --snapshotIt;

        std::vector<std::pair<std::string, double>> rankedSymbols;
        rankedSymbols.reserve(tradableSymbols.size());
        for (const auto& symbol : tradableSymbols) {
            const auto valueIt = snapshotIt->second.find(symbol);
            if (valueIt != snapshotIt->second.end() && std::isfinite(valueIt->second)) {
                rankedSymbols.push_back(*valueIt);
            }
        }

        if (rankedSymbols.empty()) {
            continue;
        }

        std::sort(rankedSymbols.begin(), rankedSymbols.end(), [](const auto& left, const auto& right) {
            if (left.second == right.second) {
                return left.first < right.first;
            }
            return left.second < right.second;
        });

        const double denominator = rankedSymbols.size() > 1
            ? static_cast<double>(rankedSymbols.size() - 1)
            : 1.0;
        for (std::size_t index = 0; index < rankedSymbols.size(); ++index) {
            const double rankScore = rankedSymbols.size() > 1
                ? static_cast<double>(index) / denominator
                : 1.0;
            auto& scoreState = scores[rankedSymbols[index].first];
            scoreState.score += allocation.weight * rankScore;
            scoreState.contributionCount += 1;
        }
    }

    std::vector<std::pair<std::string, double>> rankedResults;
    for (const auto& entry : scores) {
        if (entry.second.contributionCount > 0 && std::isfinite(entry.second.score)) {
            rankedResults.push_back({entry.first, entry.second.score});
        }
    }

    std::sort(rankedResults.begin(), rankedResults.end(), [](const auto& left, const auto& right) {
        if (left.second == right.second) {
            return left.first < right.first;
        }
        return left.second > right.second;
    });

    if (topN <= 0) {
        topN = static_cast<int>(rankedResults.size());
    }
    if (static_cast<std::size_t>(topN) < rankedResults.size()) {
        rankedResults.resize(static_cast<std::size_t>(topN));
    }

    std::vector<std::string> selectedSymbols;
    selectedSymbols.reserve(rankedResults.size());
    for (const auto& entry : rankedResults) {
        selectedSymbols.push_back(entry.first);
    }
    return selectedSymbols;
}

engine::BacktestResult runPortfolioStrategyBacktest(
    const domain::backtest::StrategyBacktestConfig& config,
    const std::vector<domain::model::Bar>& bars,
    const std::shared_ptr<domain::backtest::FactorDataProvider>& factorDataProvider,
    std::function<void(int, const std::string&)> progressCallback)
{
    if (!factorDataProvider) {
        throw std::runtime_error("组合策略回测缺少因子数据提供器");
    }

    const auto allocations = parsePortfolioAllocations(config);
    if (allocations.empty()) {
        throw std::runtime_error("组合策略缺少可用因子配置");
    }

    if (progressCallback) progressCallback(55, "加载组合因子数据...");

    std::map<std::string, std::map<std::string, std::map<std::string, double>>> factorSeriesByFactor;
    bool hasFactorData = false;
    for (const auto& allocation : allocations) {
        auto factorSeries = factorDataProvider->getFactorValuesRange(
            allocation.factorId,
            config.startDate,
            config.endDate);
        if (!factorSeries.empty()) {
            hasFactorData = true;
        }
        factorSeriesByFactor.emplace(allocation.factorId, std::move(factorSeries));
    }

    if (!hasFactorData) {
        throw std::runtime_error("组合策略回测未加载到任何因子值数据");
    }

    std::map<std::string, std::vector<const domain::model::Bar*>> barsByDate;
    for (const auto& bar : bars) {
        barsByDate[barDateKey(bar)].push_back(&bar);
    }

    if (barsByDate.empty()) {
        throw std::runtime_error("组合策略回测缺少按日行情数据");
    }

    if (progressCallback) progressCallback(70, "执行组合调仓回测...");

    engine::BacktestResult result;
    PortfolioRuntimeState runtimeState;
    runtimeState.cash = config.initialCapital;

    const int rebalanceFrequency = (std::max)(1, config.rebalanceFrequency);
    const int topN = integerStrategyParam(config.strategyParams, "top_n", static_cast<int>(allocations.size()));
    const double portfolioExposure = (std::min)(1.0, normalizedRatio(config.maxPositionRatio, 1.0));
    const double singlePositionLimit = (std::min)(1.0, normalizedRatio(config.maxSinglePositionRatio, portfolioExposure));
    const double stopLossRate = strategyRatioParam(
        config.strategyParams,
        {"stop_loss", "stopLoss", "stopLossPercent"},
        normalizedRatio(config.stopLossRate, 0.05));
    const double takeProfitRate = strategyRatioParam(
        config.strategyParams,
        {"take_profit", "takeProfit", "takeProfitPercent"},
        0.15);
    const double maxDrawdownLimit = normalizedRatio(config.maxDrawdownLimit, 0.2);
    const double level1Breaker = optionalStrategyRatioParam(config.strategyParams, {"level1Breaker"});
    const double level2Breaker = optionalStrategyRatioParam(config.strategyParams, {"level2Breaker"});
    const double level3Breaker = optionalStrategyRatioParam(config.strategyParams, {"level3Breaker"});

    double peakEquity = config.initialCapital;
    bool maxDrawdownTriggered = false;
    int breakerStage = 0;

    int dayIndex = 0;
    for (const auto& [tradeDate, dailyBars] : barsByDate) {
        foundation::Timestamp tradeTimestamp = foundation::Timestamp::from_string(tradeDate + " 15:00:00");
        for (const auto* bar : dailyBars) {
            if (!bar || bar->close <= 0.0) {
                continue;
            }
            runtimeState.latestPrices[bar->symbol] = bar->close;
            runtimeState.latestTimestamps[bar->symbol] = barTimestamp(*bar);
            if (bar->time > tradeTimestamp.to_milliseconds()) {
                tradeTimestamp = barTimestamp(*bar);
            }
        }

        std::set<std::string> riskExitedSymbols;
        for (auto& entry : runtimeState.positions) {
            const auto priceIt = runtimeState.latestPrices.find(entry.first);
            const auto timeIt = runtimeState.latestTimestamps.find(entry.first);
            if (priceIt == runtimeState.latestPrices.end() || timeIt == runtimeState.latestTimestamps.end()) {
                continue;
            }

            std::string exitNote;
            if (!shouldTriggerPortfolioRiskExit(
                    entry.second,
                    priceIt->second,
                    stopLossRate,
                    takeProfitRate,
                    exitNote)) {
                continue;
            }

            closePortfolioPosition(
                result,
                entry.first,
                priceIt->second,
                timeIt->second,
                config.commissionRate,
                config.slippageRate,
                runtimeState,
                exitNote);
            riskExitedSymbols.insert(entry.first);
        }

        const double currentEquity = calculatePortfolioEquity(runtimeState);
        peakEquity = (std::max)(peakEquity, currentEquity);
        const double currentDrawdown = peakEquity > 0.0
            ? (peakEquity - currentEquity) / peakEquity
            : 0.0;
        bool blockNewEntries = false;

        if (breakerStage < 3 && level3Breaker > 0.0 && currentDrawdown >= level3Breaker) {
            for (auto& entry : runtimeState.positions) {
                const auto priceIt = runtimeState.latestPrices.find(entry.first);
                const auto timeIt = runtimeState.latestTimestamps.find(entry.first);
                if (priceIt == runtimeState.latestPrices.end() || timeIt == runtimeState.latestTimestamps.end()) {
                    continue;
                }

                closePortfolioPosition(
                    result,
                    entry.first,
                    priceIt->second,
                    timeIt->second,
                    config.commissionRate,
                    config.slippageRate,
                    runtimeState,
                    "level3 breaker exit");
            }
            breakerStage = 3;
            blockNewEntries = true;
        } else if (breakerStage < 2 && level2Breaker > 0.0 && currentDrawdown >= level2Breaker) {
            for (auto& entry : runtimeState.positions) {
                const auto priceIt = runtimeState.latestPrices.find(entry.first);
                const auto timeIt = runtimeState.latestTimestamps.find(entry.first);
                if (priceIt == runtimeState.latestPrices.end() || timeIt == runtimeState.latestTimestamps.end()) {
                    continue;
                }

                reducePortfolioPosition(
                    result,
                    entry.first,
                    priceIt->second,
                    timeIt->second,
                    config.commissionRate,
                    config.slippageRate,
                    runtimeState,
                    0.5,
                    "level2 breaker reduce");
            }
            breakerStage = 2;
            blockNewEntries = true;
        } else if (breakerStage < 1 && level1Breaker > 0.0 && currentDrawdown >= level1Breaker) {
            breakerStage = 1;
            blockNewEntries = true;
        }

        if (!maxDrawdownTriggered && maxDrawdownLimit > 0.0 && currentDrawdown >= maxDrawdownLimit) {
            for (auto& entry : runtimeState.positions) {
                const auto priceIt = runtimeState.latestPrices.find(entry.first);
                const auto timeIt = runtimeState.latestTimestamps.find(entry.first);
                if (priceIt == runtimeState.latestPrices.end() || timeIt == runtimeState.latestTimestamps.end()) {
                    continue;
                }

                closePortfolioPosition(
                    result,
                    entry.first,
                    priceIt->second,
                    timeIt->second,
                    config.commissionRate,
                    config.slippageRate,
                    runtimeState,
                    "max drawdown exit");
            }
            maxDrawdownTriggered = true;
            blockNewEntries = true;
        }

        if (!maxDrawdownTriggered && !blockNewEntries && dayIndex % rebalanceFrequency == 0) {
            const auto selectedSymbols = selectPortfolioSymbols(
                tradeDate,
                dailyBars,
                allocations,
                factorSeriesByFactor,
                topN);

            if (!selectedSymbols.empty()) {
                const std::set<std::string> selectedSymbolSet(selectedSymbols.begin(), selectedSymbols.end());
                for (auto& entry : runtimeState.positions) {
                    if (selectedSymbolSet.find(entry.first) != selectedSymbolSet.end()) {
                        continue;
                    }
                    const auto priceIt = runtimeState.latestPrices.find(entry.first);
                    const auto timeIt = runtimeState.latestTimestamps.find(entry.first);
                    if (priceIt == runtimeState.latestPrices.end() || timeIt == runtimeState.latestTimestamps.end()) {
                        continue;
                    }
                    closePortfolioPosition(
                        result,
                        entry.first,
                        priceIt->second,
                        timeIt->second,
                        config.commissionRate,
                        config.slippageRate,
                        runtimeState,
                        "rebalance exit");
                }

                const double portfolioEquity = calculatePortfolioEquity(runtimeState);
                const double targetCapital = portfolioEquity * portfolioExposure;
                const double equalBudget = targetCapital / static_cast<double>(selectedSymbols.size());
                const double cappedBudget = portfolioEquity * singlePositionLimit;

                for (const auto& symbol : selectedSymbols) {
                    if (riskExitedSymbols.find(symbol) != riskExitedSymbols.end()) {
                        continue;
                    }

                    const auto positionIt = runtimeState.positions.find(symbol);
                    if (positionIt != runtimeState.positions.end() && positionIt->second.hasPosition()) {
                        continue;
                    }

                    const auto priceIt = runtimeState.latestPrices.find(symbol);
                    if (priceIt == runtimeState.latestPrices.end() || priceIt->second <= 0.0) {
                        continue;
                    }

                    const double tradePrice = config.slippageRate > 0.0
                        ? priceIt->second * (1.0 + config.slippageRate)
                        : priceIt->second;
                    const double desiredBudget = (std::min)(equalBudget, cappedBudget);
                    const double maxAffordableBudget = (std::min)(runtimeState.cash, desiredBudget);
                    const double grossUnitCost = tradePrice * (1.0 + config.commissionRate);
                    if (grossUnitCost <= 0.0) {
                        continue;
                    }

                    const double rawQuantity = maxAffordableBudget / grossUnitCost;
                    const double quantity = std::floor(rawQuantity / 100.0) * 100.0;
                    if (quantity <= 0.0) {
                        continue;
                    }

                    const double entryCommission = tradePrice * quantity * config.commissionRate;
                    const double totalCost = tradePrice * quantity + entryCommission;
                    if (totalCost > runtimeState.cash) {
                        continue;
                    }

                    runtimeState.cash -= totalCost;
                    runtimeState.positions[symbol] = {quantity, tradePrice, tradeTimestamp};
                }
            }
        }

        result.update_equity_curve(tradeTimestamp, calculatePortfolioEquity(runtimeState));
        ++dayIndex;
    }

    foundation::Timestamp latestTimestamp = bars.empty()
        ? foundation::Timestamp::now()
        : barTimestamp(bars.back());
    for (auto& entry : runtimeState.positions) {
        const auto priceIt = runtimeState.latestPrices.find(entry.first);
        const auto timeIt = runtimeState.latestTimestamps.find(entry.first);
        if (priceIt == runtimeState.latestPrices.end() || timeIt == runtimeState.latestTimestamps.end()) {
            continue;
        }
        if (timeIt->second > latestTimestamp) {
            latestTimestamp = timeIt->second;
        }
        closePortfolioPosition(
            result,
            entry.first,
            priceIt->second,
            timeIt->second,
            config.commissionRate,
            config.slippageRate,
            runtimeState,
            "final close");
    }

    result.update_equity_curve(latestTimestamp, calculatePortfolioEquity(runtimeState));
    result.calculate_all_metrics();
    return result;
}

} // namespace

namespace domain::backtest {

// PIMPL
class StrategyBacktestService::Impl {
public:
    Impl() {
        // 
        nextTaskId_ = 1;
        cacheManager_ = nullptr;
        stockDataProvider_ = nullptr;
        factorDataProvider_ = nullptr;
        
        // 
        backtestEngine_ = std::make_shared<engine::BacktestEngine>();
        
        qDebug() << "StrategyBacktestService::Impl initialized";
    }
    
    ~Impl() {
        // 
        stopAllTasks();
    }
    
    std::future<StrategyBacktestResult> runStrategyBacktestAsync(
        const StrategyBacktestConfig& config) {
        
        // ID
        std::string taskId = generateTaskId();
        
        // 
        auto task = std::make_shared<BacktestTask>(config);
        task->taskId = taskId;
        
        // 
        {
            std::lock_guard<std::mutex> lock(tasksMutex_);
            tasks_[taskId] = task;
        }
        
        // 
        std::future<StrategyBacktestResult> future = std::async(
            std::launch::async,
            [this, task]() {
                return executeStrategyBacktestTask(task);
            }
        );
        
        return future;
    }
    
    StrategyBacktestResult runStrategyBacktestSync(
        const StrategyBacktestConfig& config) {
        
        // 
        if (config.enableCache && cacheManager_) {
            std::string cacheKey = generateStrategyCacheKey(config);
            auto cachedResult = cacheManager_->getFromCache<StrategyBacktestResult>(cacheKey);
            if (cachedResult) {
                return *cachedResult;
            }
        }
        
        // 
        auto result = executeStrategyBacktest(config);
        
        // 
        if (config.enableCache && cacheManager_) {
            std::string cacheKey = generateStrategyCacheKey(config);
            cacheManager_->putToCache(cacheKey, result, config.cacheTTL);
        }
        
        return result;
    }
    
    std::future<std::vector<StrategyBacktestResult>> runBatchStrategyBacktestAsync(
        const std::vector<StrategyBacktestConfig>& configs) {
        
        return std::async(
            std::launch::async,
            [this, configs]() {
                std::vector<StrategyBacktestResult> results;
                results.reserve(configs.size());
                
                for (const auto& config : configs) {
                    results.push_back(runStrategyBacktestSync(config));
                }
                
                return results;
            }
        );
    }
    
    StrategyBacktestService::OptimizationResult optimizeStrategyParameters(
        const StrategyBacktestConfig& baseConfig,
        const std::map<std::string, std::pair<double, double>>& paramRanges,
        const std::string& objectiveFunction,
        int maxIterations) {
        
        OptimizationResult result;
        result.optimizationMethod = "grid_search"; // 
        
        // 
        int totalCombinations = 1;
        for (const auto& range : paramRanges) {
            // 
            totalCombinations *= 10; // 10
        }
        
        // 
        totalCombinations = std::min(totalCombinations, maxIterations);
        
        qDebug() << ":" << QString::fromStdString(objectiveFunction)
                 << ":" << maxIterations << ":" << totalCombinations;
        
        // 
        std::vector<std::map<std::string, double>> paramCombinations;
        generateParameterCombinations(baseConfig, paramRanges, paramCombinations, maxIterations);
        
        // 
        for (const auto& params : paramCombinations) {
            StrategyBacktestConfig testConfig = baseConfig;
            testConfig.strategyParams = params;
            
            // 
            auto backtestResult = runStrategyBacktestSync(testConfig);
            
            // 
            double score = calculateObjectiveScore(backtestResult, objectiveFunction);
            
            // 
            result.history.push_back({testConfig, backtestResult});
            
            // 
            if (score > result.bestScore) {
                result.bestScore = score;
                result.optimalConfig = testConfig;
                result.optimalResult = backtestResult;
            }
        }
        
        qDebug() << ":" << result.bestScore
                 << "ID:" << QString::fromStdString(result.optimalConfig.strategyId);
        
        return result;
    }
    
    StrategyBacktestService::StrategyComparisonResult compareStrategies(
        const std::vector<StrategyBacktestConfig>& configs) {
        
        StrategyComparisonResult comparisonResult;
        
        // 
        for (const auto& config : configs) {
            auto result = runStrategyBacktestSync(config);
            comparisonResult.results.push_back(result);
        }
        
        // 
        if (!comparisonResult.results.empty()) {
            // 
            double bestSharpe = -std::numeric_limits<double>::max();
            size_t bestIndex = 0;
            
            for (size_t i = 0; i < comparisonResult.results.size(); ++i) {
                double sharpe = comparisonResult.results[i].performance.sharpeRatio;
                if (sharpe > bestSharpe) {
                    bestSharpe = sharpe;
                    bestIndex = i;
                }
            }
            
            comparisonResult.bestStrategyId = comparisonResult.results[bestIndex].config.strategyId;
            
            // 
            comparisonResult.comparisonMetrics["best_sharpe_ratio"] = bestSharpe;
            comparisonResult.comparisonMetrics["average_sharpe_ratio"] = calculateAverageSharpe(comparisonResult.results);
            comparisonResult.comparisonMetrics["average_max_drawdown"] = calculateAverageMaxDrawdown(comparisonResult.results);
            comparisonResult.comparisonMetrics["average_annual_return"] = calculateAverageAnnualReturn(comparisonResult.results);
        }
        
        return comparisonResult;
    }
    
    void cancelBacktest(const std::string& taskId) {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        auto it = tasks_.find(taskId);
        if (it != tasks_.end()) {
            it->second->cancelled = true;
            it->second->status = BacktestStatus::CANCELLED;
        }
    }
    
    BacktestStatus getBacktestStatus(const std::string& taskId) {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        auto it = tasks_.find(taskId);
        if (it != tasks_.end()) {
            return it->second->status;
        }
        return BacktestStatus::FAILED;
    }
    
    StrategyBacktestService::TaskProgress getTaskProgress(const std::string& taskId) {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        auto it = tasks_.find(taskId);
        if (it != tasks_.end()) {
            auto& task = it->second;
            TaskProgress progress;
            progress.taskId = taskId;
            progress.status = task->status;
            progress.progress = task->progress;
            progress.message = task->errorMessage;
            progress.startTime = task->startTime;
            
            // 
            if (task->progress > 0) {
                auto elapsed = std::chrono::system_clock::now() - task->startTime;
                auto estimatedTotal = elapsed * 100 / task->progress;
                progress.estimatedCompletionTime = task->startTime + estimatedTotal;
            } else {
                progress.estimatedCompletionTime = std::chrono::system_clock::now();
            }
            
            return progress;
        }
        
        // 
        TaskProgress progress;
        progress.taskId = taskId;
        progress.status = BacktestStatus::FAILED;
        progress.progress = 0;
        progress.message = "Task not found";
        return progress;
    }
    
    std::vector<StrategyBacktestService::TaskProgress> getAllTaskProgress() {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        std::vector<TaskProgress> allProgress;
        allProgress.reserve(tasks_.size());
        
        for (const auto& kv : tasks_) {
            allProgress.push_back(getTaskProgress(kv.first));
        }
        
        return allProgress;
    }
    
    void cleanupCompletedTasks(int maxAgeHours) {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        auto now = std::chrono::system_clock::now();
        auto maxAge = std::chrono::hours(maxAgeHours);
        
        for (auto it = tasks_.begin(); it != tasks_.end(); ) {
            auto& task = it->second;
            bool isCompleted = (task->status == BacktestStatus::COMPLETED || 
                               task->status == BacktestStatus::FAILED || 
                               task->status == BacktestStatus::CANCELLED);
            
            if (isCompleted && (now - task->endTime) > maxAge) {
                it = tasks_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    void setDataProvider(std::shared_ptr<StockDataProvider> provider) {
        stockDataProvider_ = provider;
        qDebug() << "StrategyBacktestService: set stock data provider";
    }
    
    void setFactorProvider(std::shared_ptr<FactorDataProvider> provider) {
        factorDataProvider_ = provider;
        qDebug() << "StrategyBacktestService: set factor data provider";
    }
    
    void setCacheManager(std::shared_ptr<CacheManager> cacheManager) {
        cacheManager_ = cacheManager;
        qDebug() << "StrategyBacktestService: set cache manager";
    }
    
    void setBacktestEngine(std::shared_ptr<engine::BacktestEngine> engine) {
        backtestEngine_ = engine;
        qDebug() << "StrategyBacktestService: set backtest engine";
    }
    
private:
    // ID
    std::string generateTaskId() {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        std::stringstream ss;
        ss << "strategy_backtest_" << nextTaskId_++ << "_" 
           << foundation::Uuid{}.to_string();
        return ss.str();
    }
    
    // 
    std::string generateStrategyCacheKey(const StrategyBacktestConfig& config) {
        std::string key = "strategy_backtest:";
        key += config.strategyId + ":";
        key += config.startDate + ":";
        key += config.endDate + ":";
        key += std::to_string(config.initialCapital) + ":";
        
        // 
        for (const auto& param : config.strategyParams) {
            key += ":" + param.first + "=" + std::to_string(param.second);
        }
        
        return key;
    }
    
    // 
    void stopAllTasks() {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        for (auto& kv : tasks_) {
            kv.second->cancelled = true;
        }
    }
    
    // 
    StrategyBacktestResult executeStrategyBacktestTask(std::shared_ptr<BacktestTask> task) {
        try {
            task->status = BacktestStatus::RUNNING;
            task->progress = 10;
            
            // 
            auto result = executeStrategyBacktestWithProgress(task->config, 
                [task](int progress, const std::string& message) {
                    task->progress = progress;
                    task->statusMessage = message;
                });
            
            task->status = BacktestStatus::COMPLETED;
            task->progress = 100;
            task->endTime = std::chrono::system_clock::now();
            
            return result;
            
        } catch (const std::exception& e) {
            task->status = BacktestStatus::FAILED;
            task->errorMessage = e.what();
            task->endTime = std::chrono::system_clock::now();
            
            // 
            return StrategyBacktestResult();
        }
    }
    
    // 
    StrategyBacktestResult executeStrategyBacktestWithProgress(
        const StrategyBacktestConfig& config,
        std::function<void(int, const std::string&)> progressCallback) {
        
        StrategyBacktestResult result;
        result.config = config;
        result.startTime = std::chrono::system_clock::now();
        
        if (progressCallback) progressCallback(10, "...");
        
        // 
        if (!config.validate()) {
            throw std::runtime_error(": " + config.getValidationErrors());
        }
        
        if (!stockDataProvider_) {
            throw std::runtime_error("");
        }
        
        if (progressCallback) progressCallback(20, "...");
        
        // 
        std::vector<std::string> symbols = config.symbols;
        if (symbols.empty() && !config.universeId.empty()) {
            if (progressCallback) progressCallback(25, "解析指数成分股...");

            auto databaseProvider = std::dynamic_pointer_cast<DatabaseStockDataProvider>(stockDataProvider_);
            if (databaseProvider) {
                symbols = databaseProvider->getIndexConstituentSymbols(
                    QString::fromStdString(config.universeId),
                    QString::fromStdString(config.endDate)
                );
            } else {
                symbols = getSymbolsFromUniverse(config.universeId);
            }
        }
        
        if (symbols.empty()) {
            throw std::runtime_error(config.universeId.empty()
                ? "未提供可回测标的"
                : "所选指数在当前快照日期没有可用成分股");
        }
        
        // 
        if (!config.sectorFilters.empty() || !config.marketFilters.empty()) {
            symbols = filterSymbols(symbols, config.sectorFilters, config.marketFilters);
        }

        qDebug() << "StrategyBacktestService: resolved symbols"
                 << "count=" << static_cast<int>(symbols.size())
                 << "dataSourceMode=" << QString::fromStdString(config.dataSourceMode)
                 << "datasetId=" << config.datasetId
                 << "dateRange=" << QString::fromStdString(config.startDate)
                 << "->" << QString::fromStdString(config.endDate);
        
        if (progressCallback) progressCallback(30, "...");

        stockDataProvider_->setDataSourceContext(config.dataSourceMode, config.datasetId);

        if (progressCallback) progressCallback(40, "加载行情数据...");

        std::vector<domain::model::Bar> bars;
        const auto loadBarsStartedAt = std::chrono::steady_clock::now();
        for (const auto& symbol : symbols) {
            auto symbolBars = stockDataProvider_->getStockBars(symbol, config.startDate, config.endDate);
            bars.insert(bars.end(), symbolBars.begin(), symbolBars.end());
        }

        const auto loadBarsElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - loadBarsStartedAt).count();
        qDebug() << "StrategyBacktestService: market data loaded"
                 << "symbolCount=" << static_cast<int>(symbols.size())
                 << "barCount=" << static_cast<qulonglong>(bars.size())
                 << "elapsedMs=" << loadBarsElapsedMs;

        if (bars.empty()) {
            throw std::runtime_error("指定数据源下没有可用于回测的行情数据");
        }

        std::sort(bars.begin(), bars.end(), [](const auto& left, const auto& right) {
            if (left.time == right.time) {
                return left.symbol < right.symbol;
            }
            return left.time < right.time;
        });
    
    // 
    std::string strategyName = "MovingAverageStrategy";
    if (!config.strategyId.empty()) {
        strategyName = config.strategyId;
    }
    
    engine::BacktestResult backtestResult;
    if (isPortfolioStrategyConfig(config)) {
        qDebug() << "StrategyBacktestService: execute portfolio strategy"
                 << "strategyName=" << QString::fromStdString(strategyName)
                 << "factorIds=" << QString::fromStdString(
                        config.strategyOptions.count("portfolio_factor_ids") > 0
                            ? config.strategyOptions.at("portfolio_factor_ids")
                            : std::string())
                 << "rebalanceFrequency=" << config.rebalanceFrequency;
        backtestResult = runPortfolioStrategyBacktest(config, bars, factorDataProvider_, progressCallback);
    } else {
        // BacktestEnginerun
        backtestResult = backtestEngine_->run(
            bars,
            config.initialCapital,
            strategyName,
            config.maxPositionRatio,
            config.commissionRate,
            config.slippageRate,
            0.0,
            config.strategyParams,
            config.strategyOptions);
    }

    qDebug() << "StrategyBacktestService: engine result"
             << "strategyName=" << QString::fromStdString(strategyName)
             << "strategySubtype=" << QString::fromStdString(
                    config.strategyOptions.count("strategy_subtype") > 0
                        ? config.strategyOptions.at("strategy_subtype")
                        : std::string())
             << "tradeCount=" << static_cast<int>(backtestResult.trades().size())
             << "equityPointCount=" << static_cast<qulonglong>(backtestResult.equity_curve().size())
             << "totalReturn=" << backtestResult.performance().total_return
             << "annualReturn=" << backtestResult.performance().annual_return;
    
    result.backtestResult = std::make_shared<engine::BacktestResult>(backtestResult);
        
        if (progressCallback) progressCallback(80, "...");
        
        // 
        result.calculatePerformanceMetrics();
        
        // 
        extractTimeSeriesData(result);
        
        result.endTime = std::chrono::system_clock::now();
        auto duration = result.endTime - result.startTime;
        result.executionTime = std::chrono::duration<double>(duration).count();
        
        if (progressCallback) progressCallback(100, "");
        
        return result;
    }
    
    // 
    StrategyBacktestResult executeStrategyBacktest(const StrategyBacktestConfig& config) {
        return executeStrategyBacktestWithProgress(config, nullptr);
    }
    
    // 
    std::vector<std::string> getSymbolsFromUniverse(const std::string& universeId) {
        // 
        // TODO: 
        
        qDebug() << ":" << QString::fromStdString(universeId);
        
        // 
        return {};
    }
    
    // 
    std::vector<std::string> filterSymbols(const std::vector<std::string>& symbols,
                                          const std::vector<std::string>& sectorFilters,
                                          const std::vector<std::string>& marketFilters) {
        // 
        // TODO: 
        
        qDebug() << ": " << sectorFilters.size() 
                 << "" << marketFilters.size() << "";
        
        return symbols; // 
    }
    
    // 
    void generateParameterCombinations(const StrategyBacktestConfig& baseConfig,
                                      const std::map<std::string, std::pair<double, double>>& paramRanges,
                                      std::vector<std::map<std::string, double>>& combinations,
                                      int maxCombinations) {
        // 
        // TODO: 
        
        if (paramRanges.empty()) {
            return;
        }
        
        // 
        std::vector<std::vector<double>> paramValues;
        for (const auto& range : paramRanges) {
            double minVal = range.second.first;
            double maxVal = range.second.second;
            int steps = 5; // 5
            
            std::vector<double> values;
            for (int i = 0; i < steps; ++i) {
                double value = minVal + (maxVal - minVal) * i / (steps - 1);
                values.push_back(value);
            }
            paramValues.push_back(values);
        }
        
        // 
        // 
        generateCombinationsRecursive(paramRanges, paramValues, combinations, 
                                     {}, 0, maxCombinations);
    }
    
    void generateCombinationsRecursive(
        const std::map<std::string, std::pair<double, double>>& paramRanges,
        const std::vector<std::vector<double>>& paramValues,
        std::vector<std::map<std::string, double>>& combinations,
        std::map<std::string, double> current,
        size_t paramIndex,
        int maxCombinations) {
        
        if (paramIndex >= paramRanges.size()) {
            combinations.push_back(current);
            return;
        }
        
        if (combinations.size() >= maxCombinations) {
            return;
        }
        
        auto it = paramRanges.begin();
        std::advance(it, paramIndex);
        std::string paramName = it->first;
        
        for (double value : paramValues[paramIndex]) {
            current[paramName] = value;
            generateCombinationsRecursive(paramRanges, paramValues, combinations,
                                         current, paramIndex + 1, maxCombinations);
            
            if (combinations.size() >= maxCombinations) {
                break;
            }
        }
    }
    
    // 
    double calculateObjectiveScore(const StrategyBacktestResult& result,
                                  const std::string& objectiveFunction) {
        if (objectiveFunction == "sharpe_ratio") {
            return result.performance.sharpeRatio;
        } else if (objectiveFunction == "total_return") {
            return result.performance.totalReturn;
        } else if (objectiveFunction == "calmar_ratio") {
            return result.performance.calmarRatio;
        } else if (objectiveFunction == "information_ratio") {
            return result.performance.informationRatio;
        } else if (objectiveFunction == "win_rate") {
            return result.performance.winRate;
        } else if (objectiveFunction == "profit_factor") {
            return result.performance.profitFactor;
        }
        
        // 
        return result.performance.sharpeRatio;
    }
    
    // 
    void extractTimeSeriesData(StrategyBacktestResult& result) {
        if (!result.backtestResult) {
            return;
        }

        const auto& equityCurve = result.backtestResult->equity_curve();
        result.timeSeries.dates.clear();
        result.timeSeries.portfolioValues.clear();
        result.timeSeries.returns.clear();
        result.timeSeries.drawdowns.clear();
        result.timeSeries.positions.clear();
        result.timeSeries.cash.clear();

        result.timeSeries.dates.reserve(equityCurve.size());
        result.timeSeries.portfolioValues.reserve(equityCurve.size());
        result.timeSeries.returns.reserve(equityCurve.size());
        result.timeSeries.drawdowns.reserve(equityCurve.size());
        result.timeSeries.positions.reserve(equityCurve.size());
        result.timeSeries.cash.reserve(equityCurve.size());

        std::string lastDateLabel;
        double previousEquity = 0.0;
        for (const auto& point : equityCurve) {
            std::string timestampText = point.timestamp.to_string();
            const std::string dateLabel = timestampText.size() >= 10 ? timestampText.substr(0, 10) : timestampText;
            const double dailyPosition = point.equity - point.balance;

            if (!result.timeSeries.dates.empty() && dateLabel == lastDateLabel) {
                const std::size_t lastIndex = result.timeSeries.dates.size() - 1;
                result.timeSeries.portfolioValues[lastIndex] = point.equity;
                result.timeSeries.drawdowns[lastIndex] = -std::abs(point.drawdown);
                result.timeSeries.positions[lastIndex] = dailyPosition;
                result.timeSeries.cash[lastIndex] = point.balance;
                if (lastIndex > 0) {
                    const double baseEquity = result.timeSeries.portfolioValues[lastIndex - 1];
                    result.timeSeries.returns[lastIndex] = baseEquity > 0.0
                        ? point.equity / baseEquity - 1.0
                        : 0.0;
                } else {
                    result.timeSeries.returns[lastIndex] = 0.0;
                }
                previousEquity = point.equity;
                continue;
            }

            result.timeSeries.dates.push_back(dateLabel);
            result.timeSeries.portfolioValues.push_back(point.equity);
            result.timeSeries.drawdowns.push_back(-std::abs(point.drawdown));
            result.timeSeries.positions.push_back(dailyPosition);
            result.timeSeries.cash.push_back(point.balance);

            if (previousEquity > 0.0) {
                result.timeSeries.returns.push_back(point.equity / previousEquity - 1.0);
            } else {
                result.timeSeries.returns.push_back(0.0);
            }
            previousEquity = point.equity;
            lastDateLabel = dateLabel;
        }

        constexpr std::size_t kMaxChartPoints = 1200;
        const std::size_t pointCount = result.timeSeries.dates.size();
        if (pointCount > kMaxChartPoints) {
            const std::size_t stride = (pointCount + kMaxChartPoints - 1) / kMaxChartPoints;

            std::vector<std::string> sampledDates;
            std::vector<double> sampledPortfolioValues;
            std::vector<double> sampledReturns;
            std::vector<double> sampledDrawdowns;
            std::vector<double> sampledPositions;
            std::vector<double> sampledCash;

            sampledDates.reserve(kMaxChartPoints);
            sampledPortfolioValues.reserve(kMaxChartPoints);
            sampledReturns.reserve(kMaxChartPoints);
            sampledDrawdowns.reserve(kMaxChartPoints);
            sampledPositions.reserve(kMaxChartPoints);
            sampledCash.reserve(kMaxChartPoints);

            for (std::size_t index = 0; index < pointCount; index += stride) {
                sampledDates.push_back(result.timeSeries.dates[index]);
                sampledPortfolioValues.push_back(result.timeSeries.portfolioValues[index]);
                sampledReturns.push_back(result.timeSeries.returns[index]);
                sampledDrawdowns.push_back(result.timeSeries.drawdowns[index]);
                sampledPositions.push_back(result.timeSeries.positions[index]);
                sampledCash.push_back(result.timeSeries.cash[index]);
            }

            if (!sampledDates.empty() && sampledDates.back() != result.timeSeries.dates.back()) {
                sampledDates.push_back(result.timeSeries.dates.back());
                sampledPortfolioValues.push_back(result.timeSeries.portfolioValues.back());
                sampledReturns.push_back(result.timeSeries.returns.back());
                sampledDrawdowns.push_back(result.timeSeries.drawdowns.back());
                sampledPositions.push_back(result.timeSeries.positions.back());
                sampledCash.push_back(result.timeSeries.cash.back());
            }

            result.timeSeries.dates = std::move(sampledDates);
            result.timeSeries.portfolioValues = std::move(sampledPortfolioValues);
            result.timeSeries.returns = std::move(sampledReturns);
            result.timeSeries.drawdowns = std::move(sampledDrawdowns);
            result.timeSeries.positions = std::move(sampledPositions);
            result.timeSeries.cash = std::move(sampledCash);
        }
    }
    
    // 
    double calculateAverageSharpe(const std::vector<StrategyBacktestResult>& results) {
        if (results.empty()) return 0.0;
        
        double sum = 0.0;
        for (const auto& result : results) {
            sum += result.performance.sharpeRatio;
        }
        return sum / results.size();
    }
    
    // 
    double calculateAverageMaxDrawdown(const std::vector<StrategyBacktestResult>& results) {
        if (results.empty()) return 0.0;
        
        double sum = 0.0;
        for (const auto& result : results) {
            sum += result.performance.maxDrawdown;
        }
        return sum / results.size();
    }
    
    // 
    double calculateAverageAnnualReturn(const std::vector<StrategyBacktestResult>& results) {
        if (results.empty()) return 0.0;
        
        double sum = 0.0;
        for (const auto& result : results) {
            sum += result.performance.annualizedReturn;
        }
        return sum / results.size();
    }
    
private:
    // 
    std::shared_ptr<engine::BacktestEngine> backtestEngine_;
    
    // 
    std::shared_ptr<CacheManager> cacheManager_;
    std::shared_ptr<StockDataProvider> stockDataProvider_;
    std::shared_ptr<FactorDataProvider> factorDataProvider_;
    
    // 
    std::mutex tasksMutex_;
    std::map<std::string, std::shared_ptr<BacktestTask>> tasks_;
    std::atomic<int> nextTaskId_;
};

// StrategyBacktestService
StrategyBacktestService::StrategyBacktestService() 
    : pImpl(std::make_unique<Impl>()) {
    qDebug() << "StrategyBacktestService ";
}

StrategyBacktestService::~StrategyBacktestService() {
    qDebug() << "StrategyBacktestService ";
}

std::future<StrategyBacktestResult> StrategyBacktestService::runStrategyBacktestAsync(
    const StrategyBacktestConfig& config) {
    return pImpl->runStrategyBacktestAsync(config);
}

StrategyBacktestResult StrategyBacktestService::runStrategyBacktestSync(
    const StrategyBacktestConfig& config) {
    return pImpl->runStrategyBacktestSync(config);
}

std::future<std::vector<StrategyBacktestResult>> StrategyBacktestService::runBatchStrategyBacktestAsync(
    const std::vector<StrategyBacktestConfig>& configs) {
    return pImpl->runBatchStrategyBacktestAsync(configs);
}

StrategyBacktestService::OptimizationResult StrategyBacktestService::optimizeStrategyParameters(
    const StrategyBacktestConfig& baseConfig,
    const std::map<std::string, std::pair<double, double>>& paramRanges,
    const std::string& objectiveFunction,
    int maxIterations) {
    return pImpl->optimizeStrategyParameters(baseConfig, paramRanges, objectiveFunction, maxIterations);
}

StrategyBacktestService::StrategyComparisonResult StrategyBacktestService::compareStrategies(
    const std::vector<StrategyBacktestConfig>& configs) {
    return pImpl->compareStrategies(configs);
}

void StrategyBacktestService::cancelBacktest(const std::string& taskId) {
    pImpl->cancelBacktest(taskId);
}

BacktestStatus StrategyBacktestService::getBacktestStatus(const std::string& taskId) {
    return pImpl->getBacktestStatus(taskId);
}

StrategyBacktestService::TaskProgress StrategyBacktestService::getTaskProgress(const std::string& taskId) {
    return pImpl->getTaskProgress(taskId);
}

std::vector<StrategyBacktestService::TaskProgress> StrategyBacktestService::getAllTaskProgress() {
    return pImpl->getAllTaskProgress();
}

void StrategyBacktestService::cleanupCompletedTasks(int maxAgeHours) {
    pImpl->cleanupCompletedTasks(maxAgeHours);
}

void StrategyBacktestService::setDataProvider(std::shared_ptr<StockDataProvider> provider) {
    pImpl->setDataProvider(provider);
}

void StrategyBacktestService::setFactorProvider(std::shared_ptr<FactorDataProvider> provider) {
    pImpl->setFactorProvider(provider);
}

void StrategyBacktestService::setCacheManager(std::shared_ptr<CacheManager> cacheManager) {
    pImpl->setCacheManager(cacheManager);
}

void StrategyBacktestService::setBacktestEngine(std::shared_ptr<engine::BacktestEngine> engine) {
    pImpl->setBacktestEngine(engine);
}

// StrategyBacktestConfig
bool StrategyBacktestConfig::validate() const {
    // 
    if (strategyId.empty()) return false;
    if (startDate.empty() || endDate.empty()) return false;
    if (initialCapital <= 0) return false;
    if (commissionRate < 0 || commissionRate > 0.1) return false; // 10%
    if (slippageRate < 0 || slippageRate > 0.1) return false; // 10%
    if (taxRate < 0 || taxRate > 0.3) return false; // 30%
    if (dataSourceMode.empty()) return false;
    if (maxPositionRatio <= 0 || maxPositionRatio > 1.0) return false;
    if (maxSinglePositionRatio <= 0 || maxSinglePositionRatio > 1.0) return false;
    if (maxDrawdownLimit < 0 || maxDrawdownLimit > 1.0) return false;
    if (stopLossRate < 0 || stopLossRate > 1.0) return false;
    if (rebalanceFrequency <= 0) return false;
    if (maxThreads <= 0) return false;
    
    return true;
}

std::string StrategyBacktestConfig::getValidationErrors() const {
    std::string errors;
    
    if (strategyId.empty()) errors += "ID; ";
    if (startDate.empty()) errors += "; ";
    if (endDate.empty()) errors += "; ";
    if (initialCapital <= 0) errors += "0; ";
    if (commissionRate < 0 || commissionRate > 0.1) errors += "0-10%; ";
    if (slippageRate < 0 || slippageRate > 0.1) errors += "0-10%; ";
    if (taxRate < 0 || taxRate > 0.3) errors += "0-30%; ";
    if (dataSourceMode.empty()) errors += "dataSourceMode; ";
    if (maxPositionRatio <= 0 || maxPositionRatio > 1.0) errors += "0-1; ";
    if (maxSinglePositionRatio <= 0 || maxSinglePositionRatio > 1.0) errors += "0-1; ";
    if (maxDrawdownLimit < 0 || maxDrawdownLimit > 1.0) errors += "0-1; ";
    if (stopLossRate < 0 || stopLossRate > 1.0) errors += "0-1; ";
    if (rebalanceFrequency <= 0) errors += "0; ";
    if (maxThreads <= 0) errors += "0; ";
    
    return errors;
}

std::string StrategyBacktestConfig::toJson() const {
    json j;
    
    j["strategyId"] = strategyId;
    j["strategyName"] = strategyName;
    j["startDate"] = startDate;
    j["endDate"] = endDate;
    j["initialCapital"] = initialCapital;
    j["commissionRate"] = commissionRate;
    j["slippageRate"] = slippageRate;
    j["taxRate"] = taxRate;
    j["symbols"] = symbols;
    j["universeId"] = universeId;
    j["sectorFilters"] = sectorFilters;
    j["marketFilters"] = marketFilters;
    j["dataSourceMode"] = dataSourceMode;
    j["datasetId"] = datasetId;
    j["strategyParams"] = strategyParams;
    j["strategyOptions"] = strategyOptions;
    j["maxPositionRatio"] = maxPositionRatio;
    j["maxSinglePositionRatio"] = maxSinglePositionRatio;
    j["maxDrawdownLimit"] = maxDrawdownLimit;
    j["stopLossRate"] = stopLossRate;
    j["enableShortSelling"] = enableShortSelling;
    j["rebalanceFrequency"] = rebalanceFrequency;
    j["useMarketOnClose"] = useMarketOnClose;
    j["maxThreads"] = maxThreads;
    j["enableCache"] = enableCache;
    j["cacheTTL"] = cacheTTL;
    
    return j.dump();
}

StrategyBacktestConfig StrategyBacktestConfig::fromJson(const std::string& jsonStr) {
    StrategyBacktestConfig config;
    
    try {
        json j = json::parse(jsonStr);
        
        config.strategyId = j.value("strategyId", "");
        config.strategyName = j.value("strategyName", "");
        config.startDate = j.value("startDate", "");
        config.endDate = j.value("endDate", "");
        config.initialCapital = j.value("initialCapital", 1000000.0);
        config.commissionRate = j.value("commissionRate", 0.0003);
        config.slippageRate = j.value("slippageRate", 0.0002);
        config.taxRate = j.value("taxRate", 0.001);
        config.symbols = j.value("symbols", std::vector<std::string>());
        config.universeId = j.value("universeId", "");
        config.sectorFilters = j.value("sectorFilters", std::vector<std::string>());
        config.marketFilters = j.value("marketFilters", std::vector<std::string>());
        config.dataSourceMode = j.value("dataSourceMode", std::string("raw"));
        config.datasetId = j.value("datasetId", -1);
        config.strategyParams = j.value("strategyParams", std::map<std::string, double>());
        config.strategyOptions = j.value("strategyOptions", std::map<std::string, std::string>());
        config.maxPositionRatio = j.value("maxPositionRatio", 1.0);
        config.maxSinglePositionRatio = j.value("maxSinglePositionRatio", 0.1);
        config.maxDrawdownLimit = j.value("maxDrawdownLimit", 0.2);
        config.stopLossRate = j.value("stopLossRate", 0.05);
        config.enableShortSelling = j.value("enableShortSelling", false);
        config.rebalanceFrequency = j.value("rebalanceFrequency", 1);
        config.useMarketOnClose = j.value("useMarketOnClose", true);
        config.maxThreads = j.value("maxThreads", 4);
        config.enableCache = j.value("enableCache", true);
        config.cacheTTL = j.value("cacheTTL", 3600);
        
    } catch (const std::exception& e) {
        qWarning() << "StrategyBacktestConfig JSON:" << e.what();
    }
    
    return config;
}

// StrategyBacktestResult
std::string StrategyBacktestResult::toJson() const {
    json j;
    
    j["taskId"] = taskId;
    j["executionTime"] = executionTime;
    j["config"] = config.toJson();
    
    // 
    json perfJson;
    perfJson["totalReturn"] = performance.totalReturn;
    perfJson["annualizedReturn"] = performance.annualizedReturn;
    perfJson["volatility"] = performance.volatility;
    perfJson["sharpeRatio"] = performance.sharpeRatio;
    perfJson["sortinoRatio"] = performance.sortinoRatio;
    perfJson["calmarRatio"] = performance.calmarRatio;
    perfJson["maxDrawdown"] = performance.maxDrawdown;
    perfJson["winRate"] = performance.winRate;
    perfJson["profitFactor"] = performance.profitFactor;
    perfJson["averageWin"] = performance.averageWin;
    perfJson["averageLoss"] = performance.averageLoss;
    perfJson["alpha"] = performance.alpha;
    perfJson["beta"] = performance.beta;
    perfJson["informationRatio"] = performance.informationRatio;
    perfJson["trackingError"] = performance.trackingError;
    j["performance"] = perfJson;
    
    // 
    json tradesJson;
    tradesJson["totalTrades"] = trades.totalTrades;
    tradesJson["winningTrades"] = trades.winningTrades;
    tradesJson["losingTrades"] = trades.losingTrades;
    tradesJson["totalProfit"] = trades.totalProfit;
    tradesJson["totalLoss"] = trades.totalLoss;
    tradesJson["largestWin"] = trades.largestWin;
    tradesJson["largestLoss"] = trades.largestLoss;
    tradesJson["averageHoldingPeriod"] = trades.averageHoldingPeriod;
    j["trades"] = tradesJson;

    json tradeRecordsJson = json::array();
    if (backtestResult) {
        for (const auto& record : backtestResult->trades()) {
            json tradeJson;
            tradeJson["tradeId"] = record.trade_id.to_string();
            tradeJson["entryTime"] = record.entry_time.to_string();
            tradeJson["exitTime"] = record.exit_time.to_string();
            tradeJson["symbol"] = record.symbol;
            tradeJson["direction"] = record.direction;
            tradeJson["entryPrice"] = record.entry_price;
            tradeJson["exitPrice"] = record.exit_price;
            tradeJson["quantity"] = record.quantity;
            tradeJson["commission"] = record.commission;
            tradeJson["profit"] = record.profit;
            tradeJson["profitPct"] = record.profit_pct;
            tradeJson["notes"] = record.notes;
            tradeRecordsJson.push_back(std::move(tradeJson));
        }
    }
    j["tradeRecords"] = tradeRecordsJson;
    
    // 
    json riskJson;
    riskJson["var95"] = risk.var95;
    riskJson["cvar95"] = risk.cvar95;
    riskJson["downsideDeviation"] = risk.downsideDeviation;
    riskJson["upsideDeviation"] = risk.upsideDeviation;
    riskJson["skewness"] = risk.skewness;
    riskJson["kurtosis"] = risk.kurtosis;
    riskJson["sectorExposure"] = risk.sectorExposure;
    riskJson["factorExposure"] = risk.factorExposure;
    j["risk"] = riskJson;
    
    // 
    json tsJson;
    tsJson["dates"] = timeSeries.dates;
    tsJson["portfolioValues"] = timeSeries.portfolioValues;
    tsJson["returns"] = timeSeries.returns;
    tsJson["drawdowns"] = timeSeries.drawdowns;
    tsJson["positions"] = timeSeries.positions;
    tsJson["cash"] = timeSeries.cash;
    j["timeSeries"] = tsJson;
    
    return j.dump();
}

bool StrategyBacktestResult::saveToFile(const std::string& filepath) const {
    try {
        std::string jsonStr = toJson();
        std::ofstream file(filepath);
        if (!file.is_open()) {
            return false;
        }
        file << jsonStr;
        file.close();
        return true;
    } catch (const std::exception& e) {
        qWarning() << ":" << e.what();
        return false;
    }
}

StrategyBacktestResult StrategyBacktestResult::loadFromFile(const std::string& filepath) {
    StrategyBacktestResult result;
    
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error(": " + filepath);
        }
        
        std::string jsonStr((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        
        json j = json::parse(jsonStr);
        
        result.taskId = j.value("taskId", "");
        result.executionTime = j.value("executionTime", 0.0);
        
        // 
        std::string configJson = j.value("config", "");
        if (!configJson.empty()) {
            result.config = StrategyBacktestConfig::fromJson(configJson);
        }
        
        // 
        if (j.contains("performance")) {
            auto perfJson = j["performance"];
            result.performance.totalReturn = perfJson.value("totalReturn", 0.0);
            result.performance.annualizedReturn = perfJson.value("annualizedReturn", 0.0);
            result.performance.volatility = perfJson.value("volatility", 0.0);
            result.performance.sharpeRatio = perfJson.value("sharpeRatio", 0.0);
            result.performance.sortinoRatio = perfJson.value("sortinoRatio", 0.0);
            result.performance.calmarRatio = perfJson.value("calmarRatio", 0.0);
            result.performance.maxDrawdown = perfJson.value("maxDrawdown", 0.0);
            result.performance.winRate = perfJson.value("winRate", 0.0);
            result.performance.profitFactor = perfJson.value("profitFactor", 0.0);
            result.performance.averageWin = perfJson.value("averageWin", 0.0);
            result.performance.averageLoss = perfJson.value("averageLoss", 0.0);
            result.performance.alpha = perfJson.value("alpha", 0.0);
            result.performance.beta = perfJson.value("beta", 0.0);
            result.performance.informationRatio = perfJson.value("informationRatio", 0.0);
            result.performance.trackingError = perfJson.value("trackingError", 0.0);
        }
        
        // 
        if (j.contains("trades")) {
            auto tradesJson = j["trades"];
            result.trades.totalTrades = tradesJson.value("totalTrades", 0);
            result.trades.winningTrades = tradesJson.value("winningTrades", 0);
            result.trades.losingTrades = tradesJson.value("losingTrades", 0);
            result.trades.totalProfit = tradesJson.value("totalProfit", 0.0);
            result.trades.totalLoss = tradesJson.value("totalLoss", 0.0);
            result.trades.largestWin = tradesJson.value("largestWin", 0.0);
            result.trades.largestLoss = tradesJson.value("largestLoss", 0.0);
            result.trades.averageHoldingPeriod = tradesJson.value("averageHoldingPeriod", 0.0);
        }

        if (j.contains("tradeRecords") && j["tradeRecords"].is_array()) {
            auto engineResult = std::make_shared<engine::BacktestResult>();
            for (const auto& recordJson : j["tradeRecords"]) {
                engine::BacktestResult::TradeRecord record{};
                record.symbol = recordJson.value("symbol", std::string());
                record.direction = recordJson.value("direction", std::string());
                record.entry_price = recordJson.value("entryPrice", 0.0);
                record.exit_price = recordJson.value("exitPrice", 0.0);
                record.quantity = recordJson.value("quantity", 0.0);
                record.commission = recordJson.value("commission", 0.0);
                record.profit = recordJson.value("profit", 0.0);
                record.profit_pct = recordJson.value("profitPct", 0.0);
                record.notes = recordJson.value("notes", std::string());

                const std::string tradeId = recordJson.value("tradeId", std::string());
                if (!tradeId.empty()) {
                    record.trade_id = foundation::Uuid::from_string(tradeId);
                }

                const std::string entryTime = recordJson.value("entryTime", std::string());
                if (!entryTime.empty()) {
                    record.entry_time = foundation::Timestamp::from_string(entryTime);
                }

                const std::string exitTime = recordJson.value("exitTime", std::string());
                if (!exitTime.empty()) {
                    record.exit_time = foundation::Timestamp::from_string(exitTime);
                }

                engineResult->add_trade_record(record);
            }
            result.backtestResult = engineResult;
        }
        
        // 
        if (j.contains("risk")) {
            auto riskJson = j["risk"];
            result.risk.var95 = riskJson.value("var95", 0.0);
            result.risk.cvar95 = riskJson.value("cvar95", 0.0);
            result.risk.downsideDeviation = riskJson.value("downsideDeviation", 0.0);
            result.risk.upsideDeviation = riskJson.value("upsideDeviation", 0.0);
            result.risk.skewness = riskJson.value("skewness", 0.0);
            result.risk.kurtosis = riskJson.value("kurtosis", 0.0);
            
            if (riskJson.contains("sectorExposure")) {
                result.risk.sectorExposure = riskJson["sectorExposure"].get<std::map<std::string, double>>();
            }
            if (riskJson.contains("factorExposure")) {
                result.risk.factorExposure = riskJson["factorExposure"].get<std::map<std::string, double>>();
            }
        }
        
        // 
        if (j.contains("timeSeries")) {
            auto tsJson = j["timeSeries"];
            result.timeSeries.dates = tsJson.value("dates", std::vector<std::string>());
            result.timeSeries.portfolioValues = tsJson.value("portfolioValues", std::vector<double>());
            result.timeSeries.returns = tsJson.value("returns", std::vector<double>());
            result.timeSeries.drawdowns = tsJson.value("drawdowns", std::vector<double>());
            result.timeSeries.positions = tsJson.value("positions", std::vector<double>());
            result.timeSeries.cash = tsJson.value("cash", std::vector<double>());
        }
        
    } catch (const std::exception& e) {
        qWarning() << ":" << e.what();
    }
    
    return result;
}

void StrategyBacktestResult::calculatePerformanceMetrics() {
    if (!backtestResult) {
        return;
    }

    const auto& enginePerformance = backtestResult->performance();
    const auto& engineRisk = backtestResult->risk_metrics();
    const auto& engineTrades = backtestResult->trade_stats();
    const auto& tradeRecords = backtestResult->trades();

    performance.totalReturn = enginePerformance.total_return;
    performance.annualizedReturn = enginePerformance.annual_return;
    performance.volatility = engineRisk.volatility;
    performance.sharpeRatio = engineRisk.sharpe_ratio;
    performance.sortinoRatio = engineRisk.sortino_ratio;
    performance.calmarRatio = engineRisk.calmar_ratio;
    performance.maxDrawdown = engineRisk.max_drawdown;
    performance.winRate = engineTrades.win_rate;
    performance.profitFactor = engineTrades.profit_factor;
    performance.alpha = enginePerformance.alpha;
    performance.beta = enginePerformance.beta;
    performance.informationRatio = enginePerformance.information_ratio;
    performance.trackingError = 0.0;

    double averageWinningPct = 0.0;
    double averageLosingPct = 0.0;
    int winningCount = 0;
    int losingCount = 0;
    double totalHoldingDays = 0.0;

    for (const auto& trade : tradeRecords) {
        const double holdingDays = static_cast<double>(trade.exit_time.to_seconds() - trade.entry_time.to_seconds()) / 86400.0;
        totalHoldingDays += (std::max)(0.0, holdingDays);

        if (trade.profit > 0.0) {
            averageWinningPct += trade.profit_pct;
            ++winningCount;
        } else if (trade.profit < 0.0) {
            averageLosingPct += trade.profit_pct;
            ++losingCount;
        }
    }

    if (winningCount > 0) {
        performance.averageWin = averageWinningPct / static_cast<double>(winningCount);
    }
    if (losingCount > 0) {
        performance.averageLoss = averageLosingPct / static_cast<double>(losingCount);
    }

    trades.totalTrades = engineTrades.total_trades;
    trades.winningTrades = engineTrades.winning_trades;
    trades.losingTrades = engineTrades.losing_trades;
    trades.totalProfit = engineTrades.total_profit;
    trades.totalLoss = engineTrades.total_loss;
    trades.largestWin = engineTrades.max_profit;
    trades.largestLoss = engineTrades.max_loss;
    if (!tradeRecords.empty()) {
        trades.averageHoldingPeriod = totalHoldingDays / static_cast<double>(tradeRecords.size());
    }

    risk.var95 = engineRisk.var_95;
    risk.cvar95 = engineRisk.expected_shortfall;

    std::vector<double> returns;
    if (!timeSeries.returns.empty()) {
        returns.reserve(timeSeries.returns.size());
        for (double value : timeSeries.returns) {
            if (std::isfinite(value)) {
                returns.push_back(value);
            }
        }
    }

    if (!returns.empty()) {
        double negativeSquared = 0.0;
        double positiveSquared = 0.0;
        int negativeCount = 0;
        int positiveCount = 0;
        double mean = 0.0;
        for (double value : returns) {
            mean += value;
        }
        mean /= static_cast<double>(returns.size());

        double m2 = 0.0;
        double m3 = 0.0;
        double m4 = 0.0;
        for (double value : returns) {
            if (value < 0.0) {
                negativeSquared += value * value;
                ++negativeCount;
            } else {
                positiveSquared += value * value;
                ++positiveCount;
            }

            const double centered = value - mean;
            const double centered2 = centered * centered;
            m2 += centered2;
            m3 += centered2 * centered;
            m4 += centered2 * centered2;
        }

        if (negativeCount > 0) {
            risk.downsideDeviation = std::sqrt(negativeSquared / static_cast<double>(negativeCount));
        }
        if (positiveCount > 0) {
            risk.upsideDeviation = std::sqrt(positiveSquared / static_cast<double>(positiveCount));
        }

        if (returns.size() > 1 && m2 > 0.0) {
            const double variance = m2 / static_cast<double>(returns.size() - 1);
            const double stddev = std::sqrt(variance);
            if (stddev > 0.0) {
                risk.skewness = (m3 / static_cast<double>(returns.size())) / std::pow(stddev, 3);
                risk.kurtosis = (m4 / static_cast<double>(returns.size())) / std::pow(stddev, 4);
            }
        }
    }
}

} // namespace domain::backtest
