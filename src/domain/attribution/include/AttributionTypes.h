// AttributionTypes.h — 绩效归因结果数据结构
// 纯 C++，零 Qt 依赖，领域层独立模块
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace domain::attribution {

// ═══════════════════════════════════════════════════════════════════════════
// 板块归因
// ═══════════════════════════════════════════════════════════════════════════

/// @brief 单行业贡献
struct SectorAttribution {
    std::string sectorName;          // 行业名称 (如 "银行")
    double totalRealizedPnl{0.0};    // 已实现盈亏 (净值)
    double portfolioReturn{0.0};     // 组合在该行业的收益率 (pnl / 投入资金)
    int tradeCount{0};               // 成交笔数
    int stockCount{0};               // 涉及标的总数
    double averageWeight{0.0};       // 在组合中的平均权重 [0,1]
    double returnContribution{0.0};  // 对总收益的百分比贡献
};

// ═══════════════════════════════════════════════════════════════════════════
// 因子归因
// ═══════════════════════════════════════════════════════════════════════════

/// @brief 因子归因估计方法
enum class FactorEstimationMethod : std::uint8_t {
    Approximate = 0,  // weight × IC × coverage 近似估算 (Phase 1)
    Exact = 1,        // 基于因子暴露矩阵的精确计算 (Phase 2: 对接 FactorEngine)
};

/// @brief 单因子贡献
struct FactorAttribution {
    std::string factorId;            // 因子 ID
    double factorWeight{0.0};        // 在策略中的权重 [0,1]
    double factorIC{0.0};            // 因子 Rank IC
    double estimatedContribution{0.0}; // 估算收益贡献
    int coveredDays{0};              // 覆盖交易日数
    FactorEstimationMethod estimationMethod{FactorEstimationMethod::Approximate};
};

// ═══════════════════════════════════════════════════════════════════════════
// 择时归因 (Brinson)
// ═══════════════════════════════════════════════════════════════════════════

/// @brief 基准行业数据 (Brinson 所需, Bridge 层注入)
struct BenchmarkSectorDatum {
    std::string sectorName;
    double benchmarkWeight{0.0};     // 基准在该行业的权重
    double benchmarkReturn{0.0};     // 基准在该行业的收益率
};

/// @brief Brinson 分解结果
/// Phase 1 (简化): isSimplified=true, 仅 excessReturn 有效, 三效应置零
/// Phase 2 (完整): isSimplified=false, 完整 Brinson 四效应
struct TimingAttribution {
    double allocationEffect{0.0};    // 配置效应: Σ((w_p - w_b) * r_b)
    double selectionEffect{0.0};     // 选股效应: Σ(w_b * (r_p - r_b))
    double interactionEffect{0.0};   // 交互效应: Σ((w_p - w_b) * (r_p - r_b))
    double excessReturn{0.0};        // 超额收益 (Portfolio - Benchmark)
    double portfolioReturn{0.0};     // 组合总收益
    double benchmarkReturn{0.0};     // 基准总收益
    bool isSimplified{true};         // true = 简化模式 (仅 excessReturn)

    /// @brief 验证 Brinson 恒等式 (完整模式)
    [[nodiscard]] bool verifyIdentity(double tolerance = 1e-6) const noexcept {
        if (isSimplified) return true;
        double sum = allocationEffect + selectionEffect + interactionEffect;
        return std::abs(sum - excessReturn) < tolerance;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// 归因报告
// ═══════════════════════════════════════════════════════════════════════════

/// @brief 完整归因报告 (板块 + 因子 + 择时)
struct AttributionReport {
    std::vector<SectorAttribution> sectorBreakdown;
    std::vector<FactorAttribution> factorBreakdown;
    TimingAttribution timingBreakdown;
    bool isValid{false};

    /// @brief 返回按贡献降序排列的板块归因
    [[nodiscard]] std::vector<SectorAttribution> sectorsByContribution() const {
        auto sorted = sectorBreakdown;
        std::sort(sorted.begin(), sorted.end(),
                  [](const SectorAttribution& a, const SectorAttribution& b) {
                      return a.returnContribution > b.returnContribution;
                  });
        return sorted;
    }

    /// @brief 返回按贡献降序排列的因子归因
    [[nodiscard]] std::vector<FactorAttribution> factorsByContribution() const {
        auto sorted = factorBreakdown;
        std::sort(sorted.begin(), sorted.end(),
                  [](const FactorAttribution& a, const FactorAttribution& b) {
                      return a.estimatedContribution > b.estimatedContribution;
                  });
        return sorted;
    }
};

} // namespace domain::attribution
