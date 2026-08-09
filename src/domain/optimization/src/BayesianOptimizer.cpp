// BayesianOptimizer.cpp — 贝叶斯优化器实现
// GP 代理模型 (Matern 5/2 核) + Expected Improvement 采集函数
#include "BayesianOptimizer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <foundation/log/logging.hpp>

namespace domain::optimization {

namespace {
    constexpr double kSqrt5 = 2.23606797749979;   // √5
    constexpr double kInvSqrt2Pi = 0.3989422804014327; // 1 / √(2π)
    constexpr double kInvSqrt2 = 0.7071067811865475;   // 1 / √2
} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// 构造
// ═══════════════════════════════════════════════════════════════════════════

BayesianOptimizer::BayesianOptimizer(Config config)
    : m_config(std::move(config))
{
    // 用高精度时钟种子初始化 RNG
    auto seed = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    m_rng.seed(seed);

    if (m_config.initialRandomSamples == 0) {
        m_config.initialRandomSamples = -1; // 标记 "auto" — 在 optimize() 中按 dim 计算
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 主优化循环
// ═══════════════════════════════════════════════════════════════════════════

OptimizationResult BayesianOptimizer::optimize(
    const ParameterSpace& space,
    const ObjectiveSpec& /*objective*/,
    const TrialEvaluator& evaluator,
    int maxTrials,
    const ProgressCallback& onProgress)
{
    const auto startTime = std::chrono::high_resolution_clock::now();
    const int dim = space.dimension();

    if (dim == 0 || space.totalCombinations() == 0) {
        INTERNAL_WARN_STREAM << "[BayesianOptimizer] Empty parameter space, returning empty result";
        return OptimizationResult{};
    }

    // 自动计算初始采样数: 5 + 2*dim
    const int initialSamples = (m_config.initialRandomSamples < 0)
        ? (5 + 2 * dim)
        : m_config.initialRandomSamples;
    const int effectiveInitial = std::min(initialSamples, maxTrials > 0 ? maxTrials : initialSamples);

    INTERNAL_INFO_STREAM << "[BayesianOptimizer] Starting optimization: dim=" << dim
                         << " initialSamples=" << effectiveInitial
                         << " maxTrials=" << maxTrials
                         << " lengthScale=" << m_config.kernelLengthScale;

    OptimizationResult result;
    result.trials.reserve(maxTrials > 0 ? static_cast<std::size_t>(maxTrials) : 100);

    // 储存已评估的 ParamSet + 对应的 Eigen 行 (归一化)
    std::vector<ParamSet> observedParams;
    std::vector<Eigen::RowVectorXd> observedX;
    Eigen::VectorXd observedY;  // 动态扩展

    int trialIndex = 0;

    // ═══ Phase 1: 初始随机采样 ═══
    for (int i = 0; i < effectiveInitial; ++i) {
        if (maxTrials > 0 && trialIndex >= maxTrials) break;

        ParamSet candidate = randomSample(space);
        TrialResult trial = evaluator(candidate, trialIndex);
        trial.trialIndex = trialIndex;
        result.trials.push_back(trial);

        if (trial.isViable()) {
            observedParams.push_back(std::move(candidate));
            observedX.push_back(paramToVector(observedParams.back(), space));
            observedY.conservativeResize(observedY.size() + 1);
            observedY(observedY.size() - 1) = trial.objectiveValue;
        }

        ++trialIndex;
        if (onProgress) onProgress(trialIndex, maxTrials);
    }

    // 若所有初始试验都违规/失败, 无法建立 GP, 直接返回
    if (observedX.empty()) {
        INTERNAL_WARN_STREAM << "[BayesianOptimizer] All initial samples violated constraints, "
                            "cannot fit GP model";
        result.totalTrials = trialIndex;
        result.elapsedSeconds = std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - startTime).count();
        return result;
    }

    // ═══ Phase 2: 序贯贝叶斯优化 ═══
    int noImprovementCount = 0;

    while (maxTrials == 0 || trialIndex < maxTrials) {
        // 构建训练矩阵 X (从 observedX 逐行复制)
        Eigen::MatrixXd XMat(observedX.size(), dim);
        for (std::size_t r = 0; r < observedX.size(); ++r)
            XMat.row(static_cast<Eigen::Index>(r)) = observedX[r];

        // 2a. GP 拟合
        GpFitResult fit = gpFit(XMat, observedY);

        // 2b. 当前最优观测值
        double bestObserved = observedY.maxCoeff();

        // 2c. 通过随机候选点最大化 EI
        ParamSet bestCandidate;
        double bestEi = -1.0;
        bool foundCandidate = false;

        for (int c = 0; c < m_config.acquisitionCandidateCount; ++c) {
            ParamSet candidate = randomSample(space);
            Eigen::RowVectorXd xVec = paramToVector(candidate, space);
            GpPredictResult pred = gpPredict(XMat, fit, xVec);

            double ei = expectedImprovement(pred.mean, pred.variance, bestObserved);
            if (ei > bestEi) {
                bestEi = ei;
                bestCandidate = std::move(candidate);
                foundCandidate = true;
            }
        }

        if (!foundCandidate) {
            INTERNAL_WARN_STREAM << "[BayesianOptimizer] Failed to find valid acquisition candidate";
            break;
        }

        // 2d. 收敛检测: max EI < threshold 连续 patience 次
        if (bestEi < m_config.convergenceThreshold) {
            ++noImprovementCount;
            if (noImprovementCount >= m_config.convergencePatience) {
                INTERNAL_INFO_STREAM << "[BayesianOptimizer] Converged: max EI=" << bestEi
                                    << " < threshold=" << m_config.convergenceThreshold
                                    << " (" << noImprovementCount << " consecutive times)";
                break;
            }
        } else {
            noImprovementCount = 0;
        }

        // 2e. 评估最佳候选点
        TrialResult trial = evaluator(bestCandidate, trialIndex);
        trial.trialIndex = trialIndex;
        result.trials.push_back(trial);

        if (trial.isViable()) {
            observedParams.push_back(std::move(bestCandidate));
            observedX.push_back(paramToVector(observedParams.back(), space));
            observedY.conservativeResize(observedY.size() + 1);
            observedY(observedY.size() - 1) = trial.objectiveValue;
        }

        ++trialIndex;
        if (onProgress) onProgress(trialIndex, maxTrials);
    }

    INTERNAL_INFO_STREAM << "[BayesianOptimizer] Optimization finished: "
                         << result.trials.size() << " trials, "
                         << observedX.size() << " viable";

    // ═══ 后处理 ═══
    // 按 objectiveValue 降序排列, 违规试验沉底
    std::sort(result.trials.begin(), result.trials.end(),
              [](const TrialResult& a, const TrialResult& b) {
                  if (a.constraintViolated != b.constraintViolated)
                      return !a.constraintViolated; // 非违规在前
                  if (!a.errorMessage.empty() != !b.errorMessage.empty())
                      return a.errorMessage.empty(); // 无错误在前
                  return a.objectiveValue > b.objectiveValue; // 目标值降序
              });

    result.totalTrials = trialIndex;
    for (const auto& t : result.trials) {
        if (!t.errorMessage.empty() && t.errorMessage != "Cancelled by user")
            ++result.failedTrials;
        if (t.constraintViolated)
            ++result.constraintViolations;
    }
    result.elapsedSeconds = std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - startTime).count();

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// ParamSet ↔ Eigen 矩阵转换
// ═══════════════════════════════════════════════════════════════════════════

Eigen::MatrixXd BayesianOptimizer::paramsToMatrix(
    const std::vector<ParamSet>& params,
    const ParameterSpace& space) const
{
    const int n = static_cast<int>(params.size());
    const int d = space.dimension();
    Eigen::MatrixXd X(n, d);

    for (int i = 0; i < n; ++i) {
        Eigen::RowVectorXd row = paramToVector(params[static_cast<std::size_t>(i)], space);
        X.row(i) = row;
    }
    return X;
}

Eigen::RowVectorXd BayesianOptimizer::paramToVector(
    const ParamSet& param,
    const ParameterSpace& space) const
{
    const int d = space.dimension();
    Eigen::RowVectorXd vec(d);
    int col = 0;

    for (const auto& range : space.ranges()) {
        auto it = param.find(range.name());
        if (it == param.end()) {
            // 缺失参数 → 用 midpoint 归一化到 0.5
            vec(col++) = 0.5;
            continue;
        }

        double rawValue = it->second;
        double normalized = 0.5; // fallback

        switch (range.type()) {
        case ParamType::Int: {
            int denom = range.intMax() - range.intMin();
            normalized = (denom > 0)
                ? (rawValue - range.intMin()) / static_cast<double>(denom)
                : 0.5;
            break;
        }
        case ParamType::Double: {
            double denom = range.doubleMax() - range.doubleMin();
            normalized = (denom > 1e-12)
                ? (rawValue - range.doubleMin()) / denom
                : 0.5;
            break;
        }
        case ParamType::Bool: {
            normalized = (rawValue > 0.5) ? 1.0 : 0.0;
            break;
        }
        case ParamType::Enum: {
            const auto& options = range.enumOptions();
            int denom = static_cast<int>(options.size()) - 1;
            // enum value → index in options → normalize
            int idx = 0;
            for (std::size_t k = 0; k < options.size(); ++k) {
                if (options[k].value == static_cast<int>(rawValue)) {
                    idx = static_cast<int>(k);
                    break;
                }
            }
            normalized = (denom > 0) ? static_cast<double>(idx) / denom : 0.5;
            break;
        }
        }
        vec(col++) = std::clamp(normalized, 0.0, 1.0);
    }
    return vec;
}

// ═══════════════════════════════════════════════════════════════════════════
// Matern 5/2 核
// ═══════════════════════════════════════════════════════════════════════════

double BayesianOptimizer::matern52Kernel(double r) const noexcept
{
    // k(r) = σ² * (1 + √5*r + 5*r²/3) * exp(-√5*r)
    const double sr = kSqrt5 * r;
    const double term = 1.0 + sr + (5.0 * r * r) / 3.0;
    return m_config.kernelSignalVariance * term * std::exp(-sr);
}

Eigen::MatrixXd BayesianOptimizer::computeKernelMatrix(const Eigen::MatrixXd& X) const
{
    const Eigen::Index n = X.rows();
    Eigen::MatrixXd K(n, n);
    const double invRho = 1.0 / m_config.kernelLengthScale;

    for (Eigen::Index i = 0; i < n; ++i) {
        K(i, i) = m_config.kernelSignalVariance + m_config.noiseVariance;
        for (Eigen::Index j = i + 1; j < n; ++j) {
            double dist = (X.row(i) - X.row(j)).norm() * invRho;
            double kVal = matern52Kernel(dist);
            K(i, j) = kVal;
            K(j, i) = kVal;
        }
    }
    return K;
}

Eigen::VectorXd BayesianOptimizer::computeKernelVector(
    const Eigen::MatrixXd& X,
    const Eigen::RowVectorXd& xTest) const
{
    const Eigen::Index n = X.rows();
    Eigen::VectorXd kv(n);
    const double invRho = 1.0 / m_config.kernelLengthScale;

    for (Eigen::Index i = 0; i < n; ++i) {
        double dist = (X.row(i) - xTest).norm() * invRho;
        kv(i) = matern52Kernel(dist);
    }
    return kv;
}

// ═══════════════════════════════════════════════════════════════════════════
// GP 拟合与预测
// ═══════════════════════════════════════════════════════════════════════════

BayesianOptimizer::GpFitResult BayesianOptimizer::gpFit(
    const Eigen::MatrixXd& X,
    const Eigen::VectorXd& y) const
{
    // K = kernel_matrix + noise*I
    Eigen::MatrixXd K = computeKernelMatrix(X);

    // Cholesky 分解: K = L * L^T
    Eigen::LLT<Eigen::MatrixXd> llt(K);
    if (llt.info() != Eigen::Success) {
        // 数值问题: 增加噪声重试
        INTERNAL_WARN_STREAM << "[BayesianOptimizer] Cholesky failed, adding jitter";
        Eigen::MatrixXd K2 = K + Eigen::MatrixXd::Identity(K.rows(), K.cols()) * 1e-4;
        Eigen::LLT<Eigen::MatrixXd> llt2(K2);
        if (llt2.info() != Eigen::Success) {
            INTERNAL_ERROR_STREAM << "[BayesianOptimizer] Cholesky failed after jitter";
            return GpFitResult{};
        }
        GpFitResult result;
        result.L = llt2.matrixL();
        result.alpha = llt2.solve(y);
        return result;
    }

    GpFitResult result;
    result.L = llt.matrixL();
    // α = K^{-1} y = L^{-T} (L^{-1} y)
    result.alpha = llt.solve(y);
    return result;
}

BayesianOptimizer::GpPredictResult BayesianOptimizer::gpPredict(
    const Eigen::MatrixXd& XTrain,
    const GpFitResult& fit,
    const Eigen::RowVectorXd& xTest) const
{
    if (fit.L.rows() == 0) {
        // GP 未拟合, 返回先验 (均值=0, 方差=signalVariance)
        return GpPredictResult{0.0, m_config.kernelSignalVariance};
    }

    // k_* = kernel_vector(X_train, x_test)
    Eigen::VectorXd kStar = computeKernelVector(XTrain, xTest);

    // μ = k_*^T α
    double mean = kStar.dot(fit.alpha);

    // v = L^{-1} k_*
    Eigen::VectorXd v = fit.L.triangularView<Eigen::Lower>().solve(kStar);

    // σ² = k(x_test, x_test) - v^T v
    double kSelf = m_config.kernelSignalVariance + m_config.noiseVariance; // k(x,x)
    double variance = kSelf - v.squaredNorm();
    variance = std::max(variance, m_config.noiseVariance); // 防负

    return GpPredictResult{mean, variance};
}

// ═══════════════════════════════════════════════════════════════════════════
// Expected Improvement 采集函数
// ═══════════════════════════════════════════════════════════════════════════

double BayesianOptimizer::expectedImprovement(
    double mean, double variance, double bestObserved) const noexcept
{
    double sigma = std::sqrt(std::max(variance, 0.0));
    if (sigma < 1e-12) {
        return 0.0; // 方差为零, 无需探索
    }

    double diff = mean - bestObserved - m_config.explorationXi;
    double z = diff / sigma;

    // EI = σ * (z * Φ(z) + φ(z))
    double phi = normalPdf(z);
    double Phi = normalCdf(z);
    return sigma * (z * Phi + phi);
}

double BayesianOptimizer::normalPdf(double z) noexcept
{
    return kInvSqrt2Pi * std::exp(-0.5 * z * z);
}

double BayesianOptimizer::normalCdf(double z) noexcept
{
    // Φ(z) = 0.5 * (1 + erf(z / √2))
    return 0.5 * (1.0 + std::erf(z * kInvSqrt2));
}

// ═══════════════════════════════════════════════════════════════════════════
// 随机采样
// ═══════════════════════════════════════════════════════════════════════════

ParamSet BayesianOptimizer::randomSample(const ParameterSpace& space)
{
    ParamSet sample;
    for (const auto& range : space.ranges()) {
        double value = 0.0;
        switch (range.type()) {
        case ParamType::Int: {
            int minVal = range.intMin();
            int maxVal = range.intMax();
            int stepVal = std::max(range.intStep(), 1);
            int numSteps = (maxVal - minVal) / stepVal;
            if (numSteps < 0) numSteps = 0;
            std::uniform_int_distribution<int> dist(0, numSteps);
            value = static_cast<double>(minVal + dist(m_rng) * stepVal);
            break;
        }
        case ParamType::Double: {
            std::uniform_real_distribution<double> dist(
                range.doubleMin(), range.doubleMax());
            value = dist(m_rng);
            // 量化到步长
            if (range.doubleStep() > 0.0) {
                value = range.doubleMin()
                    + std::round((value - range.doubleMin()) / range.doubleStep())
                    * range.doubleStep();
            }
            break;
        }
        case ParamType::Bool: {
            std::uniform_int_distribution<int> dist(0, 1);
            value = static_cast<double>(dist(m_rng));
            break;
        }
        case ParamType::Enum: {
            const auto& options = range.enumOptions();
            if (!options.empty()) {
                std::uniform_int_distribution<std::size_t> dist(
                    0, options.size() - 1);
                value = static_cast<double>(options[dist(m_rng)].value);
            }
            break;
        }
        }
        sample[range.name()] = value;
    }
    return sample;
}

} // namespace domain::optimization
