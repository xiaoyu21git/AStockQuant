#include "domain/factor/include/HighFreqFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"

namespace factor {

HighFreqFactor::HighFreqFactor()
{
    factorType_ = FactorType::HIGH_FREQ;
}

CalculationResult HighFreqFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context, "高频因子需要 HistoricalView");
    }

    const CommonParams& common = params_;
    const auto symbols = effectiveSymbols(context);

    return executeWithCommonParams(
        context,
        common,
        [&]() { return context.date; },
        [&](const CommonRuntimeState&, CalculationResult& result) {
            // NOTE: 高频因子依赖分钟级量价数据（close_minute, volume_minute,
            // amount_minute, vwap_minute），当前日线数据管线未提供这些字段。
            // 数据管线就绪后，在此实现：
            // 1. 聪明钱因子 (Smart Money) — 按 S_t = |R|/V^0.25 排序取前20%VWAP
            // 2. 已实现高阶矩 — RV/RSkew/RKurt
            // 3. 量价相关系数、订单失衡率等
            // 通用框架：日内计算指标 → N日均值 → 截面标准化
            for (const auto& symbol : symbols) {
                result.values[symbol] = 0.0;
            }
            result.metadata.set("highFreqDataPending", json_helper::toJsonValue(true));
        },
        [](const CommonRuntimeState&, CalculationResult&) {},
        [&](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("frequency",
                json_helper::toJsonValue(params_.frequency));
            result.metadata.set("lookbackDays",
                json_helper::toJsonValue(params_.lookbackDays));
            result.metadata.set("aggregation",
                json_helper::toJsonValue(static_cast<int>(params_.aggregation)));
            result.metadata.set("momentType",
                json_helper::toJsonValue(static_cast<int>(params_.momentType)));
        });
}

std::shared_ptr<HighFreqFactor> HighFreqFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<HighFreqFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

DataRequirements HighFreqFactor::getDataRequirements() const
{
    DataRequirements req;
    // 占位声明：高频因子所需分钟级字段（数据管线就绪后自动生效）
    appendRequiredField(req, "close_minute");
    appendRequiredField(req, "volume_minute");
    appendRequiredField(req, "amount_minute");
    appendRequiredField(req, "vwap_minute");
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules HighFreqFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, params_.window);
    return rules;
}

void HighFreqFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config::hasCalculationConfig(config))
        params_.fromJson(config::calculationConfig(config));
    dataRequirements_ = getDataRequirements();
}

} // namespace factor
