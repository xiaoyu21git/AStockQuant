#include "domain/factor/include/CustomFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorInstanceManager.h"

namespace factor {

CustomFactor::CustomFactor()
{
    factorType_ = FactorType::CUSTOM;
}

CalculationResult CustomFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context, "自定义因子需要 HistoricalView");
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
        [](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("expression", json_helper::toJsonValue("custom"));
        });
}

std::shared_ptr<CustomFactor> CustomFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<CustomFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

DataRequirements CustomFactor::getDataRequirements() const
{
    DataRequirements req;
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules CustomFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, 1);
    return rules;
}

void CustomFactor::Params::fromJson(const foundation::json::JsonFacade& json) {
    CommonParams::fromJson(json);
    if (json.has("expression")) expression = json.get("expression").asString();
}

void CustomFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config.has("common")) params_.fromJson(config.get("common"));
}

} // namespace factor
