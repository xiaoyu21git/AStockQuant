// ObjectiveSpec.h — 优化目标规格 + 约束条件
// 从 StrategyBacktestResult 提取指标值并校验约束
#pragma once

#include "ParamDef.h"
#include "TrialResult.h"

#include "../../strategy/include/StrategySnapshotTypes.h"

#include <limits>
#include <string>
#include <vector>

namespace domain::optimization {

/// 约束条件
struct OptimizationConstraint final {
    std::string metricName;  // 指标名: "maxDrawdown", "totalTrades", "winRate"
    double minValue{0.0};    // 下限: metricValue >= minValue
    double maxValue{std::numeric_limits<double>::max()}; // 上限: metricValue <= maxValue
    bool enabled{false};
};

/// 优化目标规格
class ObjectiveSpec final {
public:
    /// 工厂: 最大化指定指标
    static ObjectiveSpec maximize(ObjectiveMetric metric);

    /// 工厂: 最大化自定义指标 (按名称从 StrategyBacktestResult 提取)
    static ObjectiveSpec maximizeCustom(std::string metricName);

    // ── 只读访问 ──

    [[nodiscard]] ObjectiveMetric metric() const noexcept { return m_metric; }
    [[nodiscard]] const std::string& customMetricName() const noexcept { return m_customMetricName; }
    [[nodiscard]] bool isMaximization() const noexcept { return m_isMaximization; }

    // ── 约束 ──

    void addConstraint(OptimizationConstraint constraint);
    [[nodiscard]] const std::vector<OptimizationConstraint>& constraints() const noexcept {
        return m_constraints;
    }

    /// 从回测结果提取目标值
    /// 若 result.success==false 返回 -inf
    [[nodiscard]] double extractValue(
        const domain::strategy::StrategyBacktestResult& result) const;

    /// 检查是否满足所有约束
    [[nodiscard]] bool checkConstraints(
        const domain::strategy::StrategyBacktestResult& result) const;

private:
    ObjectiveSpec() = default;

    ObjectiveMetric m_metric{ObjectiveMetric::SharpeRatio};
    std::string m_customMetricName;
    bool m_isMaximization{true};
    std::vector<OptimizationConstraint> m_constraints;
};

} // namespace domain::optimization
