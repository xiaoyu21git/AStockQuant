#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// FactorAttributionTypes — 因子归因类型 (因子收益法, 组合因子模式)
// 口径: contribution_i = Σ_k w̃_i(k)·r_i(k), r_i(k)=子因子第k期多空分组收益(无成本)
//       w̃_i(k) = w_i / Σ_{第k期有数据的 j} w_j (缺失日重归一)
// 恒等式(构造性成立): totalLongShortReturn = Σ contribution + residualRankingInteraction + residualCosts
// 领域层零 Qt 依赖
// ══════════════════════════════════════════════════════════════════════════════

#include <string>
#include <vector>

namespace factor {

/// @brief 因子归因行 (逐子因子)
struct FactorAttributionRow final {
    std::string factorId;
    std::string factorName;              // 域层留空, Bridge 侧用 FactorService 补显示名
    double weight{0.0};                  // 归一化静态权重 w_i/Σw_j (展示用; 逐期重归一在贡献计算内)
    double rankIcMean{0.0};              // per-child 逐期 Rank IC 均值
    double rankIcir{0.0};                // per-child Rank IC 信息比率 (均值/标准差)
    double longShortReturn{0.0};         // Σ_k r_i(k) 算术累计 (无成本口径)
    double contribution{0.0};            // Σ_k w̃_i(k)·r_i(k) 静态权重折算贡献 (无成本口径)
    int coveredDays{0};                  // IC 覆盖天数 (icSeries.size())
    std::vector<double> cumulativeContribution;  // 逐期累计贡献曲线
};

/// @brief 因子归因报告 (组合因子模式)
struct FactorAttributionReport final {
    std::vector<FactorAttributionRow> rows;   // |contribution| 降序
    double totalLongShortReturn{0.0};         // 锚点 = Σ compositeCostAdjReturns[k] (组合实际·扣费后)
    double residualRankingInteraction{0.0};   // 排名交互残差 = total − Σcontribution − residualCosts
    double residualCosts{0.0};                // 成本拖累 = Σ(costAdj − raw)[k] (逐期直取)
    int periodCount{0};                       // 组合多空收益期数
    bool isValid{false};
    std::string notice;                       // 口径/降级说明
};

} // namespace factor
