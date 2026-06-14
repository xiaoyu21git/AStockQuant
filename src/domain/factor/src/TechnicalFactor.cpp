#include "domain/factor/include/TechnicalFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorInstanceManager.h"

namespace factor {

TechnicalFactor::TechnicalFactor()
{
    factorType_ = FactorType::TECHNICAL;
}

CalculationResult TechnicalFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context, "技术因子需要 HistoricalView");
    }
    const CommonParams& common = params_;
    const int window = (std::max)(2, static_cast<int>(common.window));
    const auto symbols = effectiveSymbols(context);

    return executeWithCommonParams(
        context,
        common,
        [&]() { return context.date; },
        [&](const CommonRuntimeState& runtime, CalculationResult& result) {
            const auto closesBySymbol = context.historicalView->getBatchTimeSeries(
                symbols, runtime.effectiveDate, window + 1, {std::string("close")});
            const auto& closeMap = closesBySymbol.find("close") != closesBySymbol.end()
                ? closesBySymbol.at("close") : std::unordered_map<std::string, std::vector<double>>{};
            for (const auto& symbol : symbols) {
                auto it = closeMap.find(symbol);
                if (it == closeMap.end() || it->second.size() < 2) continue;
                const auto& vals = it->second;
                double ret = vals.back() / vals.front() - 1.0;
                if (std::isfinite(ret)) result.values[symbol] = ret;
            }
        },
        [](const CommonRuntimeState&, CalculationResult&) {},
        [&](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("indicatorType", json_helper::toJsonValue(1));
        });
}

std::shared_ptr<TechnicalFactor> TechnicalFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<TechnicalFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

DataRequirements TechnicalFactor::getDataRequirements() const
{
    DataRequirements req;
    appendRequiredField(req, "close");
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules TechnicalFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, 2);
    return rules;
}

void TechnicalFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config.has("common")) params_.fromJson(config.get("common"));
}

} // namespace factor