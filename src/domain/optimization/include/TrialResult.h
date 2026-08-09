// TrialResult.h — 单次试验结果 + 调优汇总结果
// 领域层纯 C++，零外部依赖
#pragma once

#include "ParamDef.h"

#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace domain::optimization {

/// 一组具体参数值 (paramName → value)
/// 所有类型 (int/bool/enum) 统一存储为 double
using ParamSet = std::unordered_map<std::string, double>;

/// 单次试验结果
struct TrialResult final {
    ParamSet params;                 // 参数名 → 值
    double objectiveValue{0.0};      // 目标函数值
    bool constraintViolated{false};  // 违反约束
    std::string errorMessage;        // 回测失败原因 (空=成功)
    std::int32_t trialIndex{0};

    /// ⚠️ 约束违规约定:
    /// 当 constraintViolated==true 时，objectiveValue 必须设为
    /// -std::numeric_limits<double>::infinity()，
    /// 确保排序后违规试验沉底，不会被误选为最优。
    ///
    /// 此转换由 TrialEvaluator (桥接层) 在构造 TrialResult 时统一处理:
    ///   if (!objective.checkConstraints(result)) {
    ///       trial.constraintViolated = true;
    ///       trial.objectiveValue = -std::numeric_limits<double>::infinity();
    ///   }

    [[nodiscard]] bool isViable() const noexcept {
        return !constraintViolated && errorMessage.empty();
    }
};

/// 调优完整结果
struct OptimizationResult final {
    /// 所有试验，按 objectiveValue 降序排列，违规试验沉底
    std::vector<TrialResult> trials;
    int totalTrials{0};
    int failedTrials{0};
    int constraintViolations{0};
    double elapsedSeconds{0.0};

    [[nodiscard]] bool isEmpty() const noexcept { return trials.empty(); }

    /// 首个 viable 试验 (非违规非失败中目标值最高)
    [[nodiscard]] const TrialResult* bestTrial() const {
        for (const auto& t : trials) {
            if (t.isViable()) return &t;
        }
        return nullptr;
    }
};

} // namespace domain::optimization
