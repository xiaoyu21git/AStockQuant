// ParameterSpace.cpp — 参数空间实现
#include "../include/ParameterSpace.h"

#include <algorithm>
#include <random>
#include <stdexcept>

namespace domain::optimization {

ParameterSpace::ParameterSpace(std::vector<ParamRange> ranges)
    : m_ranges(std::move(ranges)) {
}

ParameterSpace ParameterSpace::subSpace(const std::vector<std::string>& paramNames) const {
    std::vector<ParamRange> selected;
    selected.reserve(paramNames.size());
    for (const auto& name : paramNames) {
        const ParamRange* found = find(name);
        if (found) {
            selected.push_back(*found);
        }
    }
    return ParameterSpace(std::move(selected));
}

std::vector<ParamSet> ParameterSpace::generateGrid() const {
    if (m_ranges.empty()) {
        return {ParamSet{}};  // 无参数 → 1 个空组合
    }
    std::vector<ParamSet> output;
    ParamSet current;
    buildGridRecursive(m_ranges, 0, current, output);
    return output;
}

ParamSet ParameterSpace::randomSample() const {
    // 使用 thread_local 随机引擎避免每次构造
    static thread_local std::mt19937 gen(std::random_device{}());

    ParamSet sample;
    for (const auto& range : m_ranges) {
        auto candidates = range.candidateValues();
        if (candidates.empty()) continue;
        std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
        sample[range.name()] = candidates[dist(gen)];
    }
    return sample;
}

std::size_t ParameterSpace::totalCombinations() const {
    if (m_ranges.empty()) return 1;
    std::size_t total = 1;
    for (const auto& range : m_ranges) {
        int c = range.candidateCount();
        if (c <= 0) continue;
        total *= static_cast<std::size_t>(c);
    }
    return total;
}

const ParamRange* ParameterSpace::find(const std::string& name) const {
    for (const auto& range : m_ranges) {
        if (range.name() == name) {
            return &range;
        }
    }
    return nullptr;
}

// ── 递归笛卡尔积构造 ──

void ParameterSpace::buildGridRecursive(
    const std::vector<ParamRange>& ranges,
    std::size_t dimIndex,
    ParamSet& current,
    std::vector<ParamSet>& output) {

    if (dimIndex >= ranges.size()) {
        output.push_back(current);
        return;
    }

    const auto& range = ranges[dimIndex];
    auto candidates = range.candidateValues();
    for (double v : candidates) {
        current[range.name()] = v;
        buildGridRecursive(ranges, dimIndex + 1, current, output);
    }
}

} // namespace domain::optimization
