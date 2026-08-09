// BayesianOptimizer.h — 贝叶斯优化器 (高斯过程代理模型 + EI 采集函数)
// 领域层纯算法，零外部依赖(除 Eigen)，通过 TrialEvaluator 解耦回测引擎
#pragma once

#include "IOptimizer.h"

#include <Eigen/Dense>
#include <random>
#include <vector>

namespace domain::optimization {

/// 贝叶斯优化器: 基于高斯过程(GP)代理模型的序贯参数优化
///
/// 算法流程:
///   1. 初始设计: 随机采样 5 + 2*dim 个点并评估
///   2. 迭代: GP 拟合已有观测 → EI 采集函数最大化 → 评估新点 → 更新 GP
///   3. 终止: maxTrials 达到 或 最大 EI < convergenceThreshold
///
/// 核函数: Matern 5/2 — 平滑但不过度光滑, 适合中等平滑度的目标函数
/// 采集函数: Expected Improvement (EI) — 平衡探索(exploration)与利用(exploitation)
class BayesianOptimizer final : public IOptimizer {
public:
    /// 可调配置
    struct Config {
        /// 初始随机采样数 (0 = 自动: 5 + 2*dim)
        int initialRandomSamples{0};

        /// 采集函数优化用随机候选点数
        int acquisitionCandidateCount{1000};

        /// EI 探索参数 ξ: 正值鼓励更多探索
        double explorationXi{0.01};

        /// Matern 5/2 核长度尺度 ρ
        double kernelLengthScale{1.0};

        /// Matern 5/2 核信号方差 σ²
        double kernelSignalVariance{1.0};

        /// GP 观测噪声 (用于数值稳定性, 加到对角线上)
        double noiseVariance{1e-6};

        /// 收敛阈值: 当 max EI < threshold 时提前终止
        double convergenceThreshold{1e-6};

        /// 收敛连续触发次数 (连续 N 次 EI < threshold 才终止，避免抖动)
        int convergencePatience{3};
    };

    explicit BayesianOptimizer(Config config = {});
    ~BayesianOptimizer() override = default;

    [[nodiscard]] OptimizationResult optimize(
        const ParameterSpace& space,
        const ObjectiveSpec& objective,
        const TrialEvaluator& evaluator,
        int maxTrials,
        const ProgressCallback& onProgress) override;

private:
    Config m_config;

    // ── GP 内部辅助 ──

    /// 将 ParamSet 向量转换为归一化 Eigen 矩阵 (每维 min-max 归一化到 [0,1])
    Eigen::MatrixXd paramsToMatrix(const std::vector<ParamSet>& params,
                                   const ParameterSpace& space) const;

    /// 将单个 ParamSet 转换为归一化行向量
    Eigen::RowVectorXd paramToVector(const ParamSet& param,
                                     const ParameterSpace& space) const;

    /// Matern 5/2 核函数: k(r) = σ² * (1 + √5*r + 5*r²/3) * exp(-√5*r)
    /// 其中 r = ||x - y|| / ρ (归一化距离)
    double matern52Kernel(double r) const noexcept;

    /// 计算核矩阵 K(X, X) + noise * I
    Eigen::MatrixXd computeKernelMatrix(const Eigen::MatrixXd& X) const;

    /// 计算核向量 k(X, x_test)
    Eigen::VectorXd computeKernelVector(const Eigen::MatrixXd& X,
                                        const Eigen::RowVectorXd& xTest) const;

    /// GP 拟合: 返回 Cholesky 分解 L 和权重向量 α
    /// K = kernel_matrix(X, X) + noise*I
    /// L = cholesky(K) → lower triangular
    /// α = L^T \ (L \ y)
    struct GpFitResult {
        Eigen::MatrixXd L;   // Cholesky 下三角
        Eigen::VectorXd alpha; // GP 权重
    };
    GpFitResult gpFit(const Eigen::MatrixXd& X, const Eigen::VectorXd& y) const;

    /// GP 预测: 给定训练数据拟合结果, 预测 xTest 处的均值与方差
    struct GpPredictResult {
        double mean{0.0};
        double variance{0.0};
    };
    GpPredictResult gpPredict(const Eigen::MatrixXd& XTrain,
                              const GpFitResult& fit,
                              const Eigen::RowVectorXd& xTest) const;

    /// Expected Improvement 采集函数
    /// EI(x) = σ * (Z * Φ(Z) + φ(Z))  其中 Z = (μ - f_best - ξ) / σ
    double expectedImprovement(double mean, double variance,
                               double bestObserved) const noexcept;

    /// 标准正态 PDF
    static double normalPdf(double z) noexcept;

    /// 标准正态 CDF (使用 erf 近似)
    static double normalCdf(double z) noexcept;

    // ── 搜索辅助 ──

    /// 在参数空间内随机生成一个候选点
    ParamSet randomSample(const ParameterSpace& space);

    /// 随机数生成器 (线程安全用 thread_local, 这里单线程用成员)
    std::mt19937_64 m_rng;
};

} // namespace domain::optimization
