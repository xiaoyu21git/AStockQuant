// ParameterSpace.h — 多维参数搜索空间
// 支持笛卡尔积网格生成 + 随机采样
#pragma once

#include "ParamRange.h"
#include "TrialResult.h"

#include <cstddef>
#include <string>
#include <vector>

namespace domain::optimization {

/// 多维参数搜索空间
class ParameterSpace final {
public:
    ParameterSpace() = default;
    explicit ParameterSpace(std::vector<ParamRange> ranges);

    /// 获取子空间 (仅保留指定名称的参数)
    [[nodiscard]] ParameterSpace subSpace(const std::vector<std::string>& paramNames) const;

    /// 生成所有网格点 (按 ranges 顺序笛卡尔积)
    /// 空 ranges 返回 { 空 ParamSet }
    [[nodiscard]] std::vector<ParamSet> generateGrid() const;

    /// 随机采样一个点 (均匀分布)
    [[nodiscard]] ParamSet randomSample() const;

    /// 总组合数 (各维度 candidateCount 之积)
    [[nodiscard]] std::size_t totalCombinations() const;

    /// 维度数
    [[nodiscard]] int dimension() const noexcept { return static_cast<int>(m_ranges.size()); }

    /// 按名查找参数范围
    [[nodiscard]] const ParamRange* find(const std::string& name) const;

    /// 只读访问 ranges
    [[nodiscard]] const std::vector<ParamRange>& ranges() const noexcept { return m_ranges; }

    [[nodiscard]] bool isEmpty() const noexcept { return m_ranges.empty(); }

private:
    std::vector<ParamRange> m_ranges;

    /// 递归构造笛卡尔积 (内部实现)
    static void buildGridRecursive(
        const std::vector<ParamRange>& ranges,
        std::size_t dimIndex,
        ParamSet& current,
        std::vector<ParamSet>& output);
};

} // namespace domain::optimization
