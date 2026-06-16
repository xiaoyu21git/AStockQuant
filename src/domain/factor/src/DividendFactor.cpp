#include "domain/factor/include/DividendFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"

#include <cmath>

namespace factor {

DividendFactor::DividendFactor()
{
    factorType_ = FactorType::DIVIDEND;
}

CalculationResult DividendFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context, "红利因子需要 HistoricalView");
    }
    const CommonParams& common = params_;
    const auto symbols = effectiveSymbols(context);

    return executeWithCommonParams(
        context,
        common,
        [&]() { return context.date; },
        [&](const CommonRuntimeState& runtime, CalculationResult& result) {
            const auto crossSection = context.historicalView->getCrossSection(
                runtime.effectiveDate, "dividend_yield", symbols);
            for (const auto& [symbol, value] : crossSection) {
                if (!std::isfinite(value) || value < 0) continue;
                result.values[symbol] = value > 1.0 ? value / 100.0 : value;
            }
        },
        [](const CommonRuntimeState&, CalculationResult&) {},
        [](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("metricSourceTable", json_helper::toJsonValue(static_cast<int>(SourceTable::FINANCIAL_INDICATOR)));
        });
}

std::shared_ptr<DividendFactor> DividendFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<DividendFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

DataRequirements DividendFactor::getDataRequirements() const
{
    DataRequirements req;
    appendRequiredField(req, "dividend_yield");
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules DividendFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, 1);
    return rules;
}

void DividendFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config::hasCalculationConfig(config))
        params_.fromJson(config::calculationConfig(config));
    dataRequirements_ = getDataRequirements();
}

} // namespace factor