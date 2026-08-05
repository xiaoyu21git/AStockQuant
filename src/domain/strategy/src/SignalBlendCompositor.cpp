#include "../include/SignalBlendCompositor.h"

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace domain::strategy {

namespace {

/// @brief 核心算法: 扫全市场 → 过滤 → compositeScore 排名 → 输出候选池
std::vector<std::string> selectPoolCore(
    const FactorSignalProcessor& processor,
    const std::function<bool(const std::string&)>& filterFn)
{
    const auto allSyms = processor.allSymbols();
    struct Scored { std::string sym; double score; };
    std::vector<Scored> candidates;
    candidates.reserve(allSyms.size());

    for (const auto& sym : allSyms) {
        if (!filterFn(sym)) continue;
        double cs = processor.compositeScore(sym);
        if (cs < processor.minimumCompositeScore()) continue;
        candidates.push_back({sym, cs});
    }
    if (candidates.empty()) return {};

    // 按 compositeScore 降序
    std::sort(candidates.begin(), candidates.end(),
        [](const Scored& a, const Scored& b) { return a.score > b.score; });

    // 候选池 = targetPositionCount（用户直接设定）
    const int poolSize = processor.targetPositionCount();
    const int limit = (std::min)(static_cast<int>(candidates.size()), poolSize);

    std::vector<std::string> pool;
    pool.reserve(limit);
    for (int i = 0; i < limit; ++i)
        pool.push_back(std::move(candidates[i].sym));
    return pool;
}

} // anonymous namespace

// ── RankOnly: 不过滤 ──
std::vector<std::string> RankOnlyPoolSelector::selectPool(
    const FactorSignalProcessor& processor) const
{
    return selectPoolCore(processor, [](const std::string&) { return true; });
}

// ── Intersection: 所有因子 passFilter ──
std::vector<std::string> IntersectionPoolSelector::selectPool(
    const FactorSignalProcessor& processor) const
{
    return selectPoolCore(processor,
        [&processor](const std::string& sym) { return processor.passFilter(sym); });
}

// ── Union: 任一因子 passAnyFilter ──
std::vector<std::string> UnionPoolSelector::selectPool(
    const FactorSignalProcessor& processor) const
{
    return selectPoolCore(processor,
        [&processor](const std::string& sym) { return processor.passAnyFilter(sym); });
}

// ── Quota: 各因子独立排名, 按权重比例分池, 合并去重 ──
std::vector<std::string> QuotaPoolSelector::selectPool(
    const FactorSignalProcessor& processor) const
{
    const int poolSize = processor.targetPositionCount();
    const auto& influences = processor.factorInfluences();
    std::unordered_set<std::string> pool;
    std::vector<std::string> overflow;  // 去重被跳过的 → 补位

    for (const auto& [factorId, influence] : influences) {
        if (influence <= 0.0) continue;
        int quota = static_cast<int>(poolSize * influence);
        if (quota < 1) quota = 1;
        auto ranked = processor.rankedSymbols(factorId);
        int taken = 0;
        for (const auto& sym : ranked) {
            if (taken >= quota) break;
            if (pool.insert(sym).second) {
                ++taken;
            } else {
                overflow.push_back(sym);  // 重复 → 留作补位
            }
        }
    }

    // 补位: 去重导致不足 poolSize → 从溢出的标的补
    if (static_cast<int>(pool.size()) < poolSize && !overflow.empty()) {
        for (const auto& sym : overflow) {
            if (static_cast<int>(pool.size()) >= poolSize) break;
            pool.insert(sym);
        }
    }

    return std::vector<std::string>(pool.begin(), pool.end());
}

// ── Factory ──
std::unique_ptr<ICandidatePoolSelector> createPoolSelector(FactorCombineMode mode)
{
    switch (mode) {
    case FactorCombineMode::Intersection:
        return std::make_unique<IntersectionPoolSelector>();
    case FactorCombineMode::Union:
        return std::make_unique<UnionPoolSelector>();
    case FactorCombineMode::Quota:
        return std::make_unique<QuotaPoolSelector>();
    case FactorCombineMode::RankOnly:
    default:
        return std::make_unique<RankOnlyPoolSelector>();
    }
}

} // namespace domain::strategy
