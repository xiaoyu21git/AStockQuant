#include "FactorAttributionCalculator.h"

#include <algorithm>
#include <cmath>

namespace factor {

double FactorAttributionCalculator::seriesMean(const std::vector<double>& values)
{
    if (values.empty()) return 0.0;
    double sum = 0.0;
    for (double v : values) sum += v;
    return sum / static_cast<double>(values.size());
}

double FactorAttributionCalculator::seriesStd(const std::vector<double>& values, double mean)
{
    if (values.size() < 2) return 0.0;
    double variance = 0.0;
    for (double v : values) {
        const double delta = v - mean;
        variance += delta * delta;
    }
    variance /= static_cast<double>(values.size());
    return std::sqrt(variance);
}

FactorAttributionReport FactorAttributionCalculator::compute(const Inputs& inputs) const
{
    FactorAttributionReport report;
    report.periodCount = static_cast<int>(inputs.compositeCostAdjReturns.size());

    // ── 锚点与成本残差 ──
    // total = Σ costAdj (组合实际·扣费后); residualCosts = Σ(costAdj − raw) 逐期成本拖累直取
    // 两序列同循环 push 等长, 此处仍按较短板保守对齐, 余下的 costAdj 尾部单独计入 total
    const size_t k = std::min(inputs.compositeRawReturns.size(), inputs.compositeCostAdjReturns.size());
    double total = 0.0;
    double residualCosts = 0.0;
    for (size_t i = 0; i < k; ++i) {
        total += inputs.compositeCostAdjReturns[i];
        residualCosts += inputs.compositeCostAdjReturns[i] - inputs.compositeRawReturns[i];
    }
    for (size_t i = k; i < inputs.compositeCostAdjReturns.size(); ++i)
        total += inputs.compositeCostAdjReturns[i];
    report.totalLongShortReturn = total;
    report.residualCosts = residualCosts;

    // ── 静态权重归一化 (展示口径) ──
    double totalWeight = 0.0;
    for (const auto& child : inputs.children)
        totalWeight += child.weight;
    const bool weightOk = totalWeight > 0.0 && !inputs.children.empty();

    // ── 逐子因子贡献 ──
    // contribution_i = Σ_k w̃_i(k)·r_i(k), 缺失日重归一: w̃_i(k) = w_i / Σ_{第k期有数据的 j} w_j
    // 期序列与组合 raw/costAdj 同调仓日对齐 (Orchestrator 只在组合同频调仓日产出)
    double sumContribution = 0.0;
    report.rows.reserve(inputs.children.size());
    for (const auto& child : inputs.children) {
        FactorAttributionRow row;
        row.factorId = child.instanceId;
        row.weight = weightOk ? child.weight / totalWeight : 0.0;
        row.rankIcMean = seriesMean(child.icSeries);
        const double icStd = seriesStd(child.icSeries, row.rankIcMean);
        row.rankIcir = icStd > 1e-12 ? row.rankIcMean / icStd : 0.0;
        row.coveredDays = static_cast<int>(child.icSeries.size());

        const size_t n = child.longShortReturns.size();
        row.cumulativeContribution.reserve(n);
        double cumulative = 0.0;
        for (size_t t = 0; t < n; ++t) {
            double presentWeight = 0.0;
            for (const auto& other : inputs.children)
                if (t < other.longShortReturns.size())
                    presentWeight += other.weight;
            const double wtilde = presentWeight > 0.0 ? child.weight / presentWeight : 0.0;
            cumulative += wtilde * child.longShortReturns[t];
            row.cumulativeContribution.push_back(cumulative);
            row.longShortReturn += child.longShortReturns[t];
        }
        row.contribution = cumulative;
        sumContribution += cumulative;
        report.rows.push_back(std::move(row));
    }

    // ── 排名交互残差 (恒等式构造性成立的余项) ──
    report.residualRankingInteraction = total - sumContribution - residualCosts;

    // ── |贡献| 降序 ──
    std::sort(report.rows.begin(), report.rows.end(),
              [](const FactorAttributionRow& a, const FactorAttributionRow& b) {
                  return std::abs(a.contribution) > std::abs(b.contribution);
              });

    report.isValid = weightOk && report.periodCount > 0;
    report.notice = report.isValid
        ? "贡献 = 归一化静态权重 × 各期多空分组收益（无成本口径）逐期累计；组合总收益为扣费后实际口径，差额拆分入双残差行"
        : "因子归因不可用（仅组合因子模式且存在多空收益序列时产出）";
    return report;
}

} // namespace factor
