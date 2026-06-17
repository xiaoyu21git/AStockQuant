#include "domain/factor/include/LiquidityFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include <numeric>
#include <chrono>
#include <cmath>
#include <limits>
#include <ta_libc.h>

namespace factor {

LiquidityFactor::LiquidityFactor()
{
    factorType_ = FactorType::LIQUIDITY;
}

CalculationResult LiquidityFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context, "流动性因子需要 HistoricalView");
    }

    const CommonParams& common = params_;
    const LiquidityMetric metricKind = params_.liquidityMetric;
    const auto& metric = [&]() -> std::string {
        switch (metricKind) {
        case LiquidityMetric::TURNOVER_RATE: return FIELD_TURNOVER_RATE;
        case LiquidityMetric::VOLUME: return "volume";
        case LiquidityMetric::AMIHUD_ILLIQUIDITY: return FIELD_AMIHUD_ILLIQUIDITY;
        case LiquidityMetric::AMPLITUDE: return "amplitude";
        default: return "";
        }
    }();
    if (metric.empty()) {
        return createHistoricalViewRuntimeError(context, "流动性因子缺少有效 metric 枚举");
    }

    const int window = (std::max)(1, static_cast<int>(common.window));
    const auto symbols = effectiveSymbols(context);

    return executeWithCommonParams(
        context,
        common,
        [&]() { return context.date; },
        [&](const CommonRuntimeState& runtime, CalculationResult& result) {
            CalculationContext effectiveContext = context;
            effectiveContext.date = runtime.effectiveDate;
            effectiveContext.symbols = symbols;

            for (const auto& symbol : symbols) {
                auto metricSeries = context.historicalView->getSeries(
                    symbol, runtime.effectiveDate, window, metric);
                if (metricSeries.empty()) continue;

                std::vector<double> metricVals;
                metricVals.reserve(metricSeries.size());
                for (const auto& dp : metricSeries) metricVals.push_back(dp.value);

                // AMIHUD 额外需要 close + volume
                std::vector<double> closeVals, volumeVals;
                if (metricKind == LiquidityMetric::AMIHUD_ILLIQUIDITY) {
                    auto cs = context.historicalView->getSeries(
                        symbol, runtime.effectiveDate, window + 1, "close");
                    auto vs = context.historicalView->getSeries(
                        symbol, runtime.effectiveDate, window + 1, "volume");
                    if (cs.size() < 2 || vs.size() < 2) continue;
                    for (const auto& dp : cs) closeVals.push_back(dp.value);
                    for (const auto& dp : vs) volumeVals.push_back(dp.value);
                }

                double value = 0.0;
                if (metricKind == LiquidityMetric::AMIHUD_ILLIQUIDITY) {
                    double sum = 0.0;
                    for (size_t i = 1; i < closeVals.size() && i < volumeVals.size(); ++i) {
                        double ret = std::abs(closeVals[i] - closeVals[i-1])
                                   / (std::max)(1e-12, closeVals[i-1]);
                        double vol = (std::max)(1e-12, volumeVals[i]);
                        sum += ret / vol;
                    }
                    value = -sum / static_cast<double>(
                        (std::min)(closeVals.size(), volumeVals.size()) - 1);
                } else {
                    value = std::accumulate(metricVals.begin(), metricVals.end(), 0.0)
                          / static_cast<double>(metricVals.size());
                    if (metricKind == LiquidityMetric::AMPLITUDE) value = -value;
                }
                if (std::isfinite(value)) {
                    result.values[symbol] = value;
                }
            }

            if (result.values.empty()) {
                result.metadata.set("emptyReason", json_helper::toJsonValue("流动性因子没有可用数据"));
            }
        },
        [](const CommonRuntimeState&, CalculationResult&) {},
        [&](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("metric", json_helper::toJsonValue(static_cast<int>(metricKind)));
            result.metadata.set("window", json_helper::toJsonValue(window));
        });
}

std::shared_ptr<LiquidityFactor> LiquidityFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<LiquidityFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

DataRequirements LiquidityFactor::getDataRequirements() const
{
    DataRequirements requirements;
    switch (params_.liquidityMetric) {
    case LiquidityMetric::AMIHUD_ILLIQUIDITY:
        appendRequiredField(requirements, "close");
        appendRequiredField(requirements, "volume");
        requirements.sourceTable = SourceTable::DAILY_BAR;
        break;
    case LiquidityMetric::TURNOVER_RATE:
        appendRequiredField(requirements, FIELD_TURNOVER_RATE);
        requirements.sourceTable = SourceTable::DAILY_BAR;
        break;
    case LiquidityMetric::VOLUME:
        appendRequiredField(requirements, "volume");
        requirements.sourceTable = SourceTable::DAILY_BAR;
        break;
    case LiquidityMetric::AMPLITUDE:
        appendRequiredField(requirements, "amplitude");
        requirements.sourceTable = SourceTable::DAILY_BAR;
        break;
    default:
        break;
    }
    appendHistoricalNeutralizationRequirements(requirements, params_.neutralizationEnabled);
    return requirements;
}

BoundaryRules LiquidityFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    const int window = (std::max)(1, static_cast<int>(params_.window));
    rules.minDataPoints = (std::max)(rules.minDataPoints, 
        params_.liquidityMetric == LiquidityMetric::AMIHUD_ILLIQUIDITY ? window + 1 : window);
    return rules;
}

void LiquidityFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    // 与 ValueFactor 一致：从 calculation 子对象解析因子参数
    if (config::hasCalculationConfig(config)) {
        const auto& calc = config::calculationConfig(config);
        if (calc.has("liquidityMetric")) {
            const auto& val = calc.get("liquidityMetric");
            if (val.isNumber()) {
                params_.liquidityMetric = static_cast<LiquidityMetric>(val.asInt());
            }
        }
        if (calc.has("metric")) {
            const auto& val = calc.get("metric");
            if (val.isNumber()) {
                params_.liquidityMetric = static_cast<LiquidityMetric>(val.asInt());
            }
        }
        if (calc.has("window")) {
            const auto& val = calc.get("window");
            if (val.isNumber()) params_.window = static_cast<int>(val.asInt());
        }
    }
    // 更新 dataRequirements 以反映解析后的参数
    dataRequirements_ = getDataRequirements();
}

} // namespace factor