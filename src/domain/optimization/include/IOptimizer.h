// IOptimizer.h — 优化器抽象接口 + TrialEvaluator 回调类型
// 领域层纯算法，通过 TrialEvaluator 解耦回测引擎
#pragma once

#include "ParameterSpace.h"
#include "ObjectiveSpec.h"
#include "TrialResult.h"

#include <functional>

namespace domain::optimization {

/// 评估器回调: 给定一组参数 + 试验序号，返回评估结果
/// 由桥接层注入 — 内部调用 StrategyEngine::backtest()
/// @return TrialResult 含 objectiveValue 和 constraintViolated
using TrialEvaluator = std::function<TrialResult(const ParamSet&, std::int32_t trialIndex)>;

/// 进度回调: (completed, total)
using ProgressCallback = std::function<void(int, int)>;

/// 优化器抽象接口
class IOptimizer {
public:
    virtual ~IOptimizer() = default;

    /// @param space 参数搜索空间
    /// @param objective 优化目标 (用于 TrialResult 构造，由 evaluator 内部使用)
    /// @param evaluator 评估回调 (桥接层注入)
    /// @param maxTrials 最大试验次数 (0 = 网格搜索跑完所有组合)
    /// @param onProgress 进度回调 (completed, total)
    [[nodiscard]] virtual OptimizationResult optimize(
        const ParameterSpace& space,
        const ObjectiveSpec& objective,
        const TrialEvaluator& evaluator,
        int maxTrials,
        const ProgressCallback& onProgress) = 0;
};

} // namespace domain::optimization
