#include "BacktestEngine.h"
#include "BacktestDecisionRuntime.h"
#include "BacktestExecutionRuntime.h"
#include "BacktestRuntimeAssembly.h"
#include "BacktestRuleTemplateEvaluator.h"
#include "StockDataProvider.h"

#include <foundation.h>

#include <algorithm>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace domain::backtest::runtime;

struct SeriesCursor {
    std::size_t seriesIndex;
    std::size_t barIndex;
};

struct SeriesCursorCompare {
    const std::vector<std::vector<domain::model::Bar>>* series;

    bool operator()(const SeriesCursor& left, const SeriesCursor& right) const {
        const auto& leftBar = (*series)[left.seriesIndex][left.barIndex];
        const auto& rightBar = (*series)[right.seriesIndex][right.barIndex];
        if (leftBar.time == rightBar.time) {
            return leftBar.symbol > rightBar.symbol;
        }
        return leftBar.time > rightBar.time;
    }
};

std::vector<domain::model::Bar> flattenBarSeries(
    const std::vector<std::vector<domain::model::Bar>>& barSeries)
{
    std::vector<domain::model::Bar> flattenedBars;
    for (const auto& series : barSeries) {
        flattenedBars.insert(flattenedBars.end(), series.begin(), series.end());
    }
    std::sort(flattenedBars.begin(), flattenedBars.end(), [](const auto& left, const auto& right) {
        if (left.time == right.time) {
            return left.symbol < right.symbol;
        }
        return left.time < right.time;
    });
    return flattenedBars;
}

template <typename DriveLoop>
engine::BacktestResult runBacktestWithRuntime(
    double initialCapital,
    const std::string& strategyName,
    double maxPositionRatio,
    double commissionRate,
    double slippageRate,
    const std::map<std::string, double>& strategyParams,
    const std::map<std::string, std::string>& strategyOptions,
    const std::vector<domain::model::Bar>& overlayBars,
    const std::shared_ptr<domain::backtest::FactorDataProvider>& factorDataProvider,
    DriveLoop&& driveLoop)
{
    engine::BacktestResult result;
    if (overlayBars.empty()) {
        return result;
    }

    auto runtime = buildBacktestRunSupport(
        initialCapital,
        strategyName,
        maxPositionRatio,
        strategyParams,
        strategyOptions,
        overlayBars,
        factorDataProvider);
    initializeRuleTemplateSummary(result, runtime.ruleTemplateSupport);

    driveLoop(result, runtime);

    finalizeOpenPositions(result, commissionRate, slippageRate, runtime.state);
    result.calculate_all_metrics();
    return result;
}

} // namespace

namespace engine {

void BacktestEngine::setFactorProvider(std::shared_ptr<domain::backtest::FactorDataProvider> provider)
{
    factorDataProvider_ = std::move(provider);
}

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
    return runBacktestWithRuntime(
        initial_capital,
        strategy_name,
        max_position_ratio,
        commission_rate,
        slippage_rate,
        strategy_params,
        strategy_options,
        bars,
        factorDataProvider_,
        [&](BacktestResult& result, BacktestRunSupport& runtime) {
            if (!runtime.factorOverlaySupport.active()) {
                for (std::size_t i = 0; i < bars.size(); ++i) {
                    processBarImmediate(result,
                                       bars[i],
                                       runtime.profile,
                                       runtime.ruleTemplateSupport,
                                       max_position_ratio,
                                       commission_rate,
                                       slippage_rate,
                                       min_volume,
                                       runtime.state);
                }
                return;
            }

            std::vector<PendingBuyCandidate> pendingCandidates;
            long long currentBatchTime = bars.front().time;
            for (std::size_t i = 0; i < bars.size(); ++i) {
                if (bars[i].time != currentBatchTime) {
                    executePendingBuys(result,
                                       runtime.profile,
                                       max_position_ratio,
                                       commission_rate,
                                       slippage_rate,
                                       runtime.state,
                                       runtime.factorOverlaySupport,
                                       pendingCandidates);
                    currentBatchTime = bars[i].time;
                }

                processBarWithFactorOverlay(result,
                                            bars[i],
                                            runtime.profile,
                                            runtime.ruleTemplateSupport,
                                            commission_rate,
                                            slippage_rate,
                                            min_volume,
                                            runtime.state,
                                            pendingCandidates);
            }
            executePendingBuys(result,
                               runtime.profile,
                               max_position_ratio,
                               commission_rate,
                               slippage_rate,
                               runtime.state,
                               runtime.factorOverlaySupport,
                               pendingCandidates);
        });
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
    const std::vector<domain::model::Bar> flattenedBars = flattenBarSeries(barSeries);
    return runBacktestWithRuntime(
        initial_capital,
        strategy_name,
        max_position_ratio,
        commission_rate,
        slippage_rate,
        strategy_params,
        strategy_options,
        flattenedBars,
        factorDataProvider_,
        [&](BacktestResult& result, BacktestRunSupport& runtime) {
            std::priority_queue<SeriesCursor, std::vector<SeriesCursor>, SeriesCursorCompare> heap{SeriesCursorCompare{&barSeries}};
            for (std::size_t seriesIndex = 0; seriesIndex < barSeries.size(); ++seriesIndex) {
                if (!barSeries[seriesIndex].empty()) {
                    heap.push(SeriesCursor{seriesIndex, 0});
                }
            }

            while (!heap.empty()) {
                if (!runtime.factorOverlaySupport.active()) {
                    SeriesCursor current = heap.top();
                    heap.pop();

                    const auto& bar = barSeries[current.seriesIndex][current.barIndex];
                    processBarImmediate(result,
                                       bar,
                                       runtime.profile,
                                       runtime.ruleTemplateSupport,
                                       max_position_ratio,
                                       commission_rate,
                                       slippage_rate,
                                       min_volume,
                                       runtime.state);

                    const std::size_t nextBarIndex = current.barIndex + 1;
                    if (nextBarIndex < barSeries[current.seriesIndex].size()) {
                        heap.push(SeriesCursor{current.seriesIndex, nextBarIndex});
                    }
                    continue;
                }

                std::vector<PendingBuyCandidate> pendingCandidates;
                const long long batchTime = barSeries[heap.top().seriesIndex][heap.top().barIndex].time;
                while (!heap.empty()) {
                    SeriesCursor current = heap.top();
                    const auto& bar = barSeries[current.seriesIndex][current.barIndex];
                    if (bar.time != batchTime) {
                        break;
                    }
                    heap.pop();

                    processBarWithFactorOverlay(result,
                                                bar,
                                                runtime.profile,
                                                runtime.ruleTemplateSupport,
                                                commission_rate,
                                                slippage_rate,
                                                min_volume,
                                                runtime.state,
                                                pendingCandidates);

                    const std::size_t nextBarIndex = current.barIndex + 1;
                    if (nextBarIndex < barSeries[current.seriesIndex].size()) {
                        heap.push(SeriesCursor{current.seriesIndex, nextBarIndex});
                    }
                }

                executePendingBuys(result,
                                   runtime.profile,
                                   max_position_ratio,
                                   commission_rate,
                                   slippage_rate,
                                   runtime.state,
                                   runtime.factorOverlaySupport,
                                   pendingCandidates);
            }
        });
}

} // namespace engine
