#include "domain/factor/include/MacroFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorInstanceManager.h"

namespace factor {

MacroFactor::MacroFactor()
{
    factorType_ = FactorType::MACRO;
}

CalculationResult MacroFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context, "宏观因子需要 HistoricalView");
    }
    const CommonParams& common = params_;
    const auto symbols = effectiveSymbols(context);

    return executeWithCommonParams(
        context,
        common,
        [&]() { return context.date; },
        [&](const CommonRuntimeState& runtime, CalculationResult& result) {
            for (const auto& symbol : symbols) {
                result.values[symbol] = 0.0;
            }
        },
        [](const CommonRuntimeState&, CalculationResult&) {},
        [&](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("macroDimension", json_helper::toJsonValue(
                static_cast<int>(params_.macroDimensions.empty()
                    ? MacroDimension::UNKNOWN : params_.macroDimensions.front())));
        });
}

std::shared_ptr<MacroFactor> MacroFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<MacroFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

DataRequirements MacroFactor::getDataRequirements() const
{
    DataRequirements req;
    appendRequiredField(req, "close");
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules MacroFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints,
        (std::max)(1, static_cast<int>(params_.window)) * 3);
    return rules;
}

void MacroFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config.has("common")) params_.fromJson(config.get("common"));
}

} // namespace factor