#pragma once
// FactorWeightOptimizer — 多因子权重优化
// 基于因子 IC/ICIR 数据自动计算优化权重, 替代手工等权配置
// 纯 C++17, 零 Qt 依赖

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace factor {

/// @brief 权重优化方法
enum class WeightOptimizationMethod : std::uint8_t {
    EqualWeight = 0,   // 等权 (基线, 不优化)
    IcWeighted = 1,    // |IC| 加权: w_i ∝ |mean(IC_i)|
    IcirWeighted = 2,  // ICIR 加权: w_i ∝ max(0, ICIR_i)
};

/// @brief 优化输入 — 由调用方(Bridge层)从历史数据计算 IC 序列后注入
struct FactorOptimizationInput {
    std::vector<std::string> factorIds;
    // factorId → 每日 IC 值序列 (Pearson 或 Spearman, 由调用方决定)
    std::unordered_map<std::string, std::vector<double>> factorIcSeries;
    // 收益率预测窗口 (交易日), 仅用于日志/诊断, 不影响计算
    int returnHorizon{1};
    // 最少需要的 IC 数据点数
    int minTrainingDays{60};
    // 单因子最大权重上限 (0~1), 防过度集中
    double maxWeightCap{0.40};
};

/// @brief 优化输出
struct FactorOptimizationResult {
    bool success{false};
    std::string errorMessage;
    /// factorId → 优化后的权重 (sum=1.0)
    std::unordered_map<std::string, double> optimizedWeights;
    /// 各因子 IC 均值
    std::unordered_map<std::string, double> icMeans;
    /// 各因子 ICIR (IC mean / IC std)
    std::unordered_map<std::string, double> icirs;
    /// ICIR < 0 被置零的因子 ID
    std::vector<std::string> negativeIcirFactors;
    /// 高相关因子 (相关系数 > threshold), 保留 IC 更高的
    std::vector<std::string> redundantFactors;
    /// 使用的优化方法名称
    std::string method;
};

/// @brief 因子权重优化器
///
/// 使用方式:
///   1. 调用方在训练窗口内计算每个因子的每日 IC 序列
///   2. 构造 FactorOptimizationInput, 填入 IC 序列
///   3. 调用 optimize() 获取优化权重
///   4. 用 optimizedWeights 覆盖策略的 factorWeights
class FactorWeightOptimizer {
public:
    FactorWeightOptimizer() = default;

    /// @brief 运行权重优化
    /// @param input  预计算的因子 IC 序列
    /// @param method 优化方法
    [[nodiscard]] FactorOptimizationResult optimize(
        const FactorOptimizationInput& input,
        WeightOptimizationMethod method) const;

    /// @brief 计算因子间 IC 序列的 Pearson 相关系数矩阵
    /// @return [i][j] = corr(IC_i, IC_j)
    [[nodiscard]] std::vector<std::vector<double>> computeCorrelationMatrix(
        const std::unordered_map<std::string, std::vector<double>>& factorIcSeries,
        const std::vector<std::string>& factorIds) const;

    /// @brief 冗余检测: 相关系数 > threshold → 标记低 IC 的因子
    /// @param threshold  相关系数阈值 (默认 0.7)
    /// @return 被标记为冗余的因子 ID 列表
    [[nodiscard]] std::vector<std::string> detectRedundant(
        const FactorOptimizationInput& input,
        double threshold = 0.7) const;

private:
    // ── 优化方法实现 ──

    [[nodiscard]] FactorOptimizationResult optimizeEqualWeight(
        const FactorOptimizationInput& input) const;

    [[nodiscard]] FactorOptimizationResult optimizeIcWeighted(
        const FactorOptimizationInput& input) const;

    [[nodiscard]] FactorOptimizationResult optimizeIcirWeighted(
        const FactorOptimizationInput& input) const;

    // ── 工具函数 ──

    /// 计算 IC 均值和标准差, 填充 icMeans/icirs
    static void computeIcStats(
        const FactorOptimizationInput& input,
        std::unordered_map<std::string, double>& icMeans,
        std::unordered_map<std::string, double>& icirs);

    /// 应用 maxWeightCap 约束并重新归一化
    static void applyWeightCap(
        std::unordered_map<std::string, double>& weights,
        double maxWeightCap);

    /// 归一化权重到 sum=1.0, 全零时 fallback 等权
    static void normalizeWeights(
        std::unordered_map<std::string, double>& weights,
        const std::vector<std::string>& factorIds);

    /// Pearson 相关系数
    static double pearsonCorrelation(
        const std::vector<double>& x,
        const std::vector<double>& y);
};

} // namespace factor
