#pragma once

#include "IStrategyService.h"

#include <memory>
#include <optional>

namespace domain::strategies {
class MultiFactorSelectionStrategy;
}

namespace domain::strategy {

struct MultiFactorRuntimeEngineSetup final {
    std::shared_ptr<const ::domain::strategies::MultiFactorSelectionStrategy> strategyDefinition;
    StrategyInstanceId strategyInstanceId{0};
    RuntimeStrategyContext context;
    CallbackRuntimeFactorServiceAdapter::Callbacks factorCallbacks;
    rules::RuleSetId ruleSetId{rules::kRuleSetAllPass};
};

[[nodiscard]] std::shared_ptr<IRuntimeStrategy> createMultiFactorSelectionRuntimeStrategy(
    std::shared_ptr<const ::domain::strategies::MultiFactorSelectionStrategy> strategyDefinition,
    StrategyInstanceId strategyInstanceId,
    CallbackRuntimeFactorServiceAdapter::Callbacks factorCallbacks);

[[nodiscard]] std::unique_ptr<StrategyEngine> createMultiFactorRuntimeEngine(
    MultiFactorRuntimeEngineSetup setup);

} // namespace domain::strategy