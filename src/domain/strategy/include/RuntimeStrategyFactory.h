#pragma once

#include "IStrategyService.h"

#include <memory>
#include <optional>

namespace domain::strategies {
class MultiFactorSelectionStrategy;
}

namespace domain::strategy {

/// @brief 创建多因子选择运行时策略
/// @note 因子数据通过 IRuntimeFactorService::copySnapshots 注入，
/// 不再需要 factorCallbacks 参数。
[[nodiscard]] std::shared_ptr<IRuntimeStrategy> createMultiFactorSelectionRuntimeStrategy(
    std::shared_ptr<const ::domain::strategies::MultiFactorSelectionStrategy> strategyDefinition,
    StrategyInstanceId strategyInstanceId);

struct MultiFactorRuntimeEngineSetup final {
    std::shared_ptr<const ::domain::strategies::MultiFactorSelectionStrategy> strategyDefinition;
    StrategyInstanceId strategyInstanceId{0};
    RuntimeStrategyContext context;
    rules::RuleSetId ruleSetId{rules::kRuleSetAllPass};
};

[[nodiscard]] std::unique_ptr<StrategyEngine> createMultiFactorRuntimeEngine(
    MultiFactorRuntimeEngineSetup setup);

} // namespace domain::strategy
