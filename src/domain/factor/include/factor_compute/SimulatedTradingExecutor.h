#pragma once

#include "FactorSignalTypes.h"
#include "GroupedBacktestTypes.h"
#include "IMarketDataView.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace factor::compute {

/// @brief 模拟成交执行器（域层，零 Qt 依赖）
///
/// 输入：按日期×标的的因子值 + close 价格矩阵
/// 输出：分组回测结果 + 执行指标
///
/// 算法：
///  1. 每日按因子值排序标的
///  2. 等分为 N 组
///  3. 计算每组的 T→T+1 日收益（过滤极端值）
///  4. 策略日收益 = 各组收益均值
///  5. 累积净值 → 年化收益/波动率/夏普/最大回撤
class SimulatedTradingExecutor final {
public:
    /// 因子值按日期和标的存储
    /// key=dateStr (YYYY-MM-DD), value=map{symbolStr → factorValue}
    using FactorValuesByDate = std::unordered_map<std::string, std::unordered_map<std::string, double>>;

    /// 价格横截面：key=dateStr, value=map{symbolStr → closePrice}
    using PriceCrossSection = std::unordered_map<std::string, std::unordered_map<std::string, double>>;

    /// @param params 分组参数
    explicit SimulatedTradingExecutor(const SimulatedTradingParams& params);

    /// 执行模拟成交
    /// @param factorValues 按日期×标的的因子值
    /// @param sortedDates 已排序的调仓日期列表（升序 YYYY-MM-DD）
    /// @param sortedDateRows 与 sortedDates 一一对应：各调仓日在 priceView（全交易日矩阵）中的行号
    /// @param priceView close 价格矩阵视图（行为全交易日，非仅调仓日）
    /// @param preAdjustView 前复权因子矩阵（adjustPriceType=="pre" 时生效，空视图=不复权）
    /// @param postAdjustView 后复权因子矩阵（adjustPriceType=="post" 时生效）
    /// @param instrumentIds 标的ID列表（uint32_t 值对应 priceView 中的列索引映射密钥）
    /// @param instrumentIdToSymbol 标的ID到字符串符号的映射
    /// @return 模拟成交结果
    SimulatedTradingResult execute(
        const FactorValuesByDate& factorValues,
        const std::vector<std::string>& sortedDates,
        const std::vector<int32_t>& sortedDateRows,
        NumericConstMatrixView priceView,
        NumericConstMatrixView preAdjustView,
        NumericConstMatrixView postAdjustView,
        const std::vector<InstrumentId>& instrumentIds,
        const std::unordered_map<uint32_t, std::string>& instrumentIdToSymbol) const;

private:
    /// @brief 按方向与 longOnly 填充本期多空篮子
    /// ranked 按因子值升序排列; longOnly 时 outShort 留空 (禁止做空, 策略收益=多头净收益)
    void fillBaskets(const std::vector<std::pair<std::string, double>>& ranked,
                     size_t n, size_t groupSize,
                     std::unordered_set<std::string>& outLong,
                     std::unordered_set<std::string>& outShort) const;

    /// @brief 策略原始收益: longOnly 时仅多头腿, 否则多空价差
    double composeRawReturn(double longRaw, double shortRaw) const;

    /// @brief 策略净收益 (换手成本按"只对换手部分扣费"口径): longOnly 时仅多头腿
    double composeNetReturn(double longRaw, double shortRaw,
                            double longCost, double shortCost) const;

    /// @brief 策略换手率: longOnly 时仅多头腿, 否则多空平均
    double composeTurnover(double longTurnover, double shortTurnover) const;

    SimulatedTradingParams params_;
};

} // namespace factor::compute