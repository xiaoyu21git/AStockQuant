#include "BacktestRuntimeAssembly.h"

#include "BacktestRuleTemplateEvaluator.h"

#include <stdexcept>

namespace {

using namespace domain::backtest::runtime;

RuleTemplateRuntimeSupport buildRuleTemplateRuntimeSupport(
    const std::string& strategyName,
    const std::map<std::string, double>& strategyParams,
    const std::map<std::string, std::string>& strategyOptions)
{
    RuleTemplateRuntimeSupport support;

    const QVariantList bindings = domain::backtest::rules::bindingListFromStrategyOptions(strategyOptions);
    if (bindings.isEmpty()) {
        return support;
    }

    QString templateError;
    support.compiledTemplates = domain::backtest::rules::loadCompiledRuleTemplates(bindings, &templateError);
    if (support.compiledTemplates.isEmpty()) {
        throw std::runtime_error(templateError.toStdString());
    }

    support.baseFacts = domain::backtest::rules::flatFactsFromStrategyConfig(strategyOptions, strategyParams);
    support.strategyScope = domain::backtest::rules::strategyScopeFromBacktestConfig(
        strategyName,
        strategyOptions,
        strategyParams);
    return support;
}

} // namespace

namespace domain::backtest::runtime {

BacktestRunSupport buildBacktestRunSupport(
    double initialCapital,
    const std::string& strategyName,
    double maxPositionRatio,
    const std::map<std::string, double>& strategyParams,
    const std::map<std::string, std::string>& strategyOptions,
    const std::vector<domain::model::Bar>& overlayBars,
    const std::shared_ptr<domain::backtest::FactorDataProvider>& factorDataProvider)
{
    BacktestRunSupport support;
    support.state.cash = initialCapital;
    support.profile = buildStrategyProfile(strategyName, maxPositionRatio, strategyParams, strategyOptions);
    support.ruleTemplateSupport = buildRuleTemplateRuntimeSupport(strategyName, strategyParams, strategyOptions);
    support.factorOverlaySupport = buildFactorOverlayRuntimeSupport(strategyOptions, overlayBars, factorDataProvider);
    return support;
}

} // namespace domain::backtest::runtime