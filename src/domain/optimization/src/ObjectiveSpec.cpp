// ObjectiveSpec.cpp — 优化目标规格实现
#include "../include/ObjectiveSpec.h"

#include <cmath>
#include <stdexcept>

namespace domain::optimization {

// ── 工厂 ──

ObjectiveSpec ObjectiveSpec::maximize(ObjectiveMetric metric) {
    if (metric == ObjectiveMetric::Custom) {
        throw std::invalid_argument(
            "ObjectiveSpec::maximize: use maximizeCustom() for custom metric");
    }
    ObjectiveSpec spec;
    spec.m_metric = metric;
    spec.m_isMaximization = true;
    return spec;
}

ObjectiveSpec ObjectiveSpec::maximizeCustom(std::string metricName) {
    ObjectiveSpec spec;
    spec.m_metric = ObjectiveMetric::Custom;
    spec.m_customMetricName = std::move(metricName);
    spec.m_isMaximization = true;
    return spec;
}

// ── 约束 ──

void ObjectiveSpec::addConstraint(OptimizationConstraint constraint) {
    m_constraints.push_back(std::move(constraint));
}

// ── 目标值提取 ──

double ObjectiveSpec::extractValue(
    const domain::strategy::StrategyBacktestResult& result) const {

    if (!result.success) {
        return -std::numeric_limits<double>::infinity();
    }

    const auto& m = result.metrics;

    switch (m_metric) {
    case ObjectiveMetric::SharpeRatio:
        return m.sharpeRatio;
    case ObjectiveMetric::AnnualizedReturn:
        return m.annualizedReturn;
    case ObjectiveMetric::CalmarRatio:
        return m.calmarRatio;
    case ObjectiveMetric::SortinoRatio:
        return m.sortinoRatio;
    case ObjectiveMetric::ProfitFactor:
        return m.profitFactor;
    case ObjectiveMetric::Custom:
        // 自定义指标按名称从 metrics 匹配 (扩展点)
        if (m_customMetricName == "winRate")       return m.winRate;
        if (m_customMetricName == "alpha")         return m.alpha;
        if (m_customMetricName == "informationRatio") return m.informationRatio;
        if (m_customMetricName == "totalReturn")   return m.totalReturn;
        // 未匹配的自定义指标返回 0
        return 0.0;
    }

    return 0.0;
}

// ── 约束检查 ──

bool ObjectiveSpec::checkConstraints(
    const domain::strategy::StrategyBacktestResult& result) const {

    if (!result.success) return false;

    for (const auto& c : m_constraints) {
        if (!c.enabled) continue;

        double metricValue = 0.0;

        // 匹配指标名
        if (c.metricName == "maxDrawdown") {
            metricValue = result.metrics.maxDrawdown;
        } else if (c.metricName == "totalTrades") {
            metricValue = static_cast<double>(result.tradeStats.totalTrades);
        } else if (c.metricName == "winRate") {
            metricValue = result.metrics.winRate;
        } else if (c.metricName == "profitFactor") {
            metricValue = result.metrics.profitFactor;
        } else if (c.metricName == "sharpeRatio") {
            metricValue = result.metrics.sharpeRatio;
        } else if (c.metricName == "annualizedReturn") {
            metricValue = result.metrics.annualizedReturn;
        } else {
            // 未知指标名，跳过检查
            continue;
        }

        if (metricValue < c.minValue) return false;
        if (metricValue > c.maxValue) return false;
    }

    return true;
}

} // namespace domain::optimization
