// GridSearchOptimizer.cpp — 网格搜索优化器实现
#include "../include/GridSearchOptimizer.h"

#include <algorithm>
#include <chrono>
#include <limits>

namespace domain::optimization {

OptimizationResult GridSearchOptimizer::optimize(
    const ParameterSpace& space,
    const ObjectiveSpec& /*objective*/,
    const TrialEvaluator& evaluator,
    int maxTrials,
    const ProgressCallback& onProgress) {

    OptimizationResult result;
    const auto startTime = std::chrono::steady_clock::now();

    // 生成所有网格点
    auto grid = space.generateGrid();

    // 限制试验次数
    int totalCombinations = static_cast<int>(grid.size());
    int effectiveTrials = totalCombinations;
    if (maxTrials > 0 && effectiveTrials > maxTrials) {
        effectiveTrials = maxTrials;
    }

    result.totalTrials = effectiveTrials;

    // 枚举所有组合并评估
    result.trials.reserve(static_cast<std::size_t>(effectiveTrials));

    for (int i = 0; i < effectiveTrials; ++i) {
        TrialResult trial = evaluator(grid[static_cast<std::size_t>(i)], i);

        // 统计
        if (trial.constraintViolated) {
            ++result.constraintViolations;
        }
        if (!trial.errorMessage.empty() && !trial.constraintViolated) {
            ++result.failedTrials;
            // 失败试验也标记为不合法
            trial.objectiveValue = -std::numeric_limits<double>::infinity();
        }

        result.trials.push_back(std::move(trial));

        // 进度回调
        if (onProgress) {
            onProgress(i + 1, effectiveTrials);
        }
    }

    // 排序: 按 objectiveValue 降序 (违规和失败的在 -inf, 自然沉底)
    std::sort(result.trials.begin(), result.trials.end(),
        [](const TrialResult& a, const TrialResult& b) {
            return a.objectiveValue > b.objectiveValue;
        });

    // 计时
    const auto endTime = std::chrono::steady_clock::now();
    result.elapsedSeconds = std::chrono::duration<double>(endTime - startTime).count();

    return result;
}

} // namespace domain::optimization
