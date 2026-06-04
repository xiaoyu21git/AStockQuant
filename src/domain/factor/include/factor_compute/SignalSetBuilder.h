#pragma once

#include "FactorSignalTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace factor::compute {

/// @brief 从 factorValuesByDate 构建 SignalSet 的纯域层工具
///
/// 将按日期×标的的字符串因子值转为扁平化的 SignalSet 矩阵，
/// 供 AnalysisModule::analyze() 使用。
class SignalSetBuilder final {
public:
    using FactorValuesByDate = std::unordered_map<std::string, std::unordered_map<std::string, double>>;

    /// 从 factorValuesByDate 构建 SignalSet
    /// @param factorValues 按日期(YYYY-MM-DD)标(符号串)的因子值
    /// @param dates SignalSet 的日期列表 (DateKey 数组)
    /// @param dateStrs 日期字符串列表 (YYYY-MM-DD, 与 dates 一一对应)
    /// @param instruments SignalSet 的标的列表
    /// @return 构建完成的 SignalSet
    static SignalSet build(
        const FactorValuesByDate& factorValues,
        const std::vector<DateKey>& dates,
        const std::vector<std::string>& dateStrs,
        const std::vector<InstrumentId>& instruments);
};

} // namespace factor::compute