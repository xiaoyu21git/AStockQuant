#include "domain/factor/include/ReversalFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace factor {

ReversalFactor::ReversalFactor()
{
    factorType_ = FactorType::REVERSAL;
}

CalculationResult ReversalFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context, "反转因子需要 HistoricalView");
    }

    const CommonParams& common = params_;
    const auto symbols = effectiveSymbols(context);
    const int window = (std::max)(2, static_cast<int>(params_.window));
    const ReversalSplitMethod method = params_.splitMethod;

    return executeWithCommonParams(
        context,
        common,
        [&]() { return context.date; },
        [&](const CommonRuntimeState& runtime, CalculationResult& result) {
            if (method == ReversalSplitMethod::W_CUT) {
                calculateWCut(context, runtime.effectiveDate, symbols, result.values);
            } else {
                // 传统反转：M = -Return(window)
                for (const auto& symbol : symbols) {
                    auto series = context.historicalView->getSeries(
                        symbol, runtime.effectiveDate, window + 1, "close");
                    if (series.size() < 2) continue;
                    const double first = series.front().value;
                    const double last  = series.back().value;
                    if (std::isfinite(first) && std::isfinite(last) && first > 0.0 && last > 0.0) {
                        const double ret = -(last / first - 1.0);
                        if (std::isfinite(ret)) result.values[symbol] = ret;
                    }
                }
            }
            if (result.values.empty()) {
                result.metadata.set("emptyReason",
                    json_helper::toJsonValue("反转因子没有可用数据"));
            }
        },
        [](const CommonRuntimeState&, CalculationResult&) {},
        [&](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("splitMethod",
                json_helper::toJsonValue(static_cast<int>(method)));
            result.metadata.set("window",
                json_helper::toJsonValue(window));
        });
}

void ReversalFactor::calculateWCut(
    const CalculationContext& context,
    const std::string& effectiveDate,
    const std::vector<std::string>& symbols,
    std::unordered_map<std::string, double>& outValues) const
{
    const int window = params_.window;
    const std::string metricField = params_.splitMetric;
    // 回溯足够日历天覆盖 window 个交易日
    const std::string startDate = BaseFactor::subtractCalendarDays(
        effectiveDate, window * 2 + 5);

    // 检查 splitMetric 字段是否存在
    const bool hasMetric = context.historicalView->hasField(metricField);
    if (!hasMetric) {
        return; // 数据未就绪，返回空
    }

    for (const auto& symbol : symbols) {
        auto priceSeries = context.historicalView->getSeries(
            symbol, startDate, effectiveDate, "close");
        auto amountSeries = context.historicalView->getSeries(
            symbol, startDate, effectiveDate, metricField);

        // 序列对齐校验：长度不足则跳过
        if (priceSeries.size() < static_cast<size_t>(window)
            || amountSeries.size() < static_cast<size_t>(window)) continue;
        if (priceSeries.size() != amountSeries.size()) continue;

        // 逐日计算收益率与成交额排序
        struct DayInfo {
            double ret;
            double amount;
        };
        std::vector<DayInfo> days;
        days.reserve(priceSeries.size() - 1);
        for (size_t i = 0; i < priceSeries.size() - 1; ++i) {
            const double prevClose = priceSeries[i].value;
            const double currClose = priceSeries[i + 1].value;
            const double amt = amountSeries[i].value;
            if (!std::isfinite(prevClose) || !std::isfinite(currClose)
                || prevClose <= 0.0 || !std::isfinite(amt)) continue;
            days.push_back({(currClose - prevClose) / prevClose, amt});
        }
        if (days.size() < static_cast<size_t>(window)) continue;

        // 取最近 window 个有效交易日
        if (days.size() > static_cast<size_t>(window)) {
            days.erase(days.begin(), days.end() - window);
        }

        // 按成交额排序，前 N/2 为高D组
        std::sort(days.begin(), days.end(),
            [](const DayInfo& a, const DayInfo& b) { return a.amount > b.amount; });

        const size_t half = window / 2;
        double mHigh = 0.0, mLow = 0.0;
        for (size_t i = 0; i < half; ++i) mHigh += days[i].ret;
        for (size_t i = half; i < days.size(); ++i) mLow += days[i].ret;

        // M = M_high - M_low，取负使因子正向（值越大越好，ascending=true）
        const double m = params_.useHighOnly ? -mHigh : -(mHigh - mLow);
        if (std::isfinite(m)) outValues[symbol] = m;
    }
}

std::shared_ptr<ReversalFactor> ReversalFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<ReversalFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

DataRequirements ReversalFactor::getDataRequirements() const
{
    DataRequirements req;
    appendRequiredField(req, "close");
    if (params_.splitMethod == ReversalSplitMethod::W_CUT) {
        appendRequiredField(req, params_.splitMetric);
    }
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules ReversalFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, params_.window);
    return rules;
}

void ReversalFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config::hasCalculationConfig(config))
        params_.fromJson(config::calculationConfig(config));
    dataRequirements_ = getDataRequirements();
}

} // namespace factor
