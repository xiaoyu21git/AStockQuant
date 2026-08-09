// ParameterSpaceFactory.h — 策略类型 → 默认可调参数空间工厂
// 按 StrategyType 返回该策略类型的所有可调参数及其默认范围
#pragma once

#include "ParamRange.h"
#include "../../strategies/include/StrategyDefinitionTypes.h"

#include <cstddef>
#include <vector>

namespace domain::optimization {

class ParameterSpaceFactory final {
public:
    /// 默认组合爆炸警告阈值
    static constexpr std::size_t kDefaultWarningThreshold = 10000;

    /// 按策略类型返回默认可调参数范围
    [[nodiscard]] static std::vector<ParamRange> defaultRanges(
        domain::strategies::StrategyType strategyType);

    /// 通用参数 (所有策略类型共有，对应设计文档 4.1 节)
    [[nodiscard]] static std::vector<ParamRange> commonRanges();

    /// 估算搜索空间大小 (各维度 candidateCount 之积)
    [[nodiscard]] static std::size_t estimateSearchSpace(
        const std::vector<ParamRange>& ranges);

    /// 检查是否会组合爆炸 (超出阈值)
    [[nodiscard]] static bool wouldExplode(
        const std::vector<ParamRange>& ranges,
        std::size_t threshold = kDefaultWarningThreshold);
};

} // namespace domain::optimization
