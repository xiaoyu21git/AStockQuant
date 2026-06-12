#pragma once

#include "IStrategyService.h"

#include <memory>
#include <optional>

namespace domain::strategies {
class MultiFactorSelectionStrategy;
}

namespace domain::strategy {

/// @brief 构建因子回调，桥接 IFactorSvc 到策略引擎。
[[nodiscard]] CallbackRuntimeFactorServiceAdapter::Callbacks buildFactorCallbacks(
    const std::vector<::domain::strategies::FactorId>& factorIds,
    std::shared_ptr<IFactorSvc> factorSvc);

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