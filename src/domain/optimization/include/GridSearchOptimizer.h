// GridSearchOptimizer.h — 网格搜索优化器
// 穷举笛卡尔积全组合，按 objectiveValue 降序排列
#pragma once

#include "IOptimizer.h"

namespace domain::optimization {

class GridSearchOptimizer final : public IOptimizer {
public:
    GridSearchOptimizer() = default;

    [[nodiscard]] OptimizationResult optimize(
        const ParameterSpace& space,
        const ObjectiveSpec& objective,
        const TrialEvaluator& evaluator,
        int maxTrials,
        const ProgressCallback& onProgress) override;
};

} // namespace domain::optimization
