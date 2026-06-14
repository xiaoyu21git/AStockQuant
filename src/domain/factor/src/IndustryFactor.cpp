#include "domain/factor/include/IndustryFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorInstanceManager.h"

namespace factor {

IndustryFactor::IndustryFactor()
{
    factorType_ = FactorType::INDUSTRY;
}

CalculationResult IndustryFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context, "行业因子需要 HistoricalView");
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
            result.metadata.set("sectorType", json_helper::toJsonValue(static_cast<int>(params_.sectorType)));
        });
}

std::shared_ptr<IndustryFactor> IndustryFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<IndustryFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

DataRequirements IndustryFactor::getDataRequirements() const
{
    DataRequirements req;
    appendRequiredField(req, "industry_code");
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules IndustryFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, 1);
    return rules;
}

void IndustryFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config.has("common")) params_.fromJson(config.get("common"));
}

} // namespace factor