#pragma once

#include <cstdint>
#include <vector>

#include "../../factor/include/factor_compute/FactorSignalTypes.h"

namespace domain::backtest {

/// @brief 矩阵 PnL 计算引擎：使用纯矩阵运算替代逐日迭代
///
/// 设计文档 Phase 4 要求：
/// - 输入为 SignalSet + 行情矩阵，输出为一次性损益结果
/// - 所有运算为向量化矩阵操作，无逐行循环
/// - 滑点/手续费亦为向量化计算
///
/// 使用方式：
/// ```cpp
/// MatrixPnLEngine engine;
/// auto result = engine.computePnL(spec);
/// ```
class MatrixPnLEngine final {
public:
    /// @brief 计算参数
    struct PnLSpec final {
        /// 信号矩阵（T x N，float32）
        std::vector<factor::compute::signal_value_t> signalMatrix;
        /// 收盘价矩阵（T x N）
        std::vector<factor::compute::signal_value_t> closePrices;
        /// 最高价矩阵（T x N，用于滑点估算）
        std::vector<factor::compute::signal_value_t> highPrices;
        /// 最低价矩阵（T x N）
        std::vector<factor::compute::signal_value_t> lowPrices;
        /// 成交量矩阵（T x N）
        std::vector<factor::compute::signal_value_t> volumes;
        int32_t timeCount{0};          ///< 时间维度 T
        int32_t instrumentCount{0};    ///< 标的维度 N
        double slippageRate{0.0001};   ///< 滑点比率
        double commissionRate{0.0003}; ///< 手续费比率
        double initialCash{1.0e6};     ///< 初始资金

        [[nodiscard]] bool isValid() const noexcept
        {
            return timeCount > 0 && instrumentCount > 0
                && signalMatrix.size() == static_cast<size_t>(timeCount) * static_cast<size_t>(instrumentCount)
                && closePrices.size() == signalMatrix.size();
        }
    };

    /// @brief 计算结果
    struct PnLResult final {
        /// 每日损益（T 维向量）
        std::vector<factor::compute::signal_value_t> dailyPnL;
        /// 累计损益（T 维向量）
        std::vector<factor::compute::signal_value_t> cumulativePnL;
        /// 每日持仓权重（T x N 矩阵）
        std::vector<factor::compute::signal_value_t> positions;
        /// 每日换手率（T 维向量）
        std::vector<factor::compute::signal_value_t> turnover;
        /// 汇总指标
        double sharpeRatio{0.0};
        double maxDrawdown{0.0};
        double annualizedReturn{0.0};
        double totalReturn{0.0};

        [[nodiscard]] bool isValid() const noexcept
        {
            return !dailyPnL.empty() && dailyPnL.size() == cumulativePnL.size();
        }
    };

    /// @brief 一次性矩阵损益计算
    /// @param spec 输入参数（信号 + 行情）
    /// @return PnL 结果（含每日序列 + 汇总指标）
    [[nodiscard]] PnLResult computePnL(const PnLSpec& spec);
};

} // namespace domain::backtest