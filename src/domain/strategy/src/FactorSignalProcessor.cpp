#include "FactorSignalProcessor.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace domain::strategy {

std::vector<std::string> FactorSignalProcessor::factorIds() const
{
    std::vector<std::string> ids;
    auto appendUnique = [&ids](const std::string& fid) {
        if (std::find(ids.begin(), ids.end(), fid) == ids.end()) ids.push_back(fid);
    };
    for (const auto& f : m_filters) appendUnique(f.factorId);
    for (const auto& s : m_scalers) appendUnique(s.factorId);
    return ids;
}

void FactorSignalProcessor::updateSnapshot(
    const std::string& factorId,
    const std::unordered_map<std::string, double>& factorValues)
{
    // 按因子独立存储 symbol→value 快照, 并预计算统计量:
    // 值先排序再求和 → 浮点加法顺序确定 → 跨进程/重启结果可复现
    // (旧实现每笔订单遍历 unordered_map 求均值方差: O(N)/单 且哈希顺序致不可复现)
    FactorSnapshotStats stats;
    stats.values.reserve(factorValues.size());
    std::vector<double> sortedValues;
    sortedValues.reserve(factorValues.size());
    for (const auto& [sym, val] : factorValues) {
        if (!std::isfinite(val)) continue;
        stats.values[sym] = val;
        sortedValues.push_back(val);
    }
    std::sort(sortedValues.begin(), sortedValues.end());

    const std::size_t count = sortedValues.size();
    if (count >= 2) {
        double sum = 0.0;
        for (double v : sortedValues) sum += v;
        stats.mean = sum / static_cast<double>(count);
        double sqSum = 0.0;
        for (double v : sortedValues) {
            const double diff = v - stats.mean;
            sqSum += diff * diff;
        }
        stats.stdev = std::sqrt(sqSum / static_cast<double>(count));
    }

    // 过滤阈值: 该因子配置的 minPercentile 分位对应的值下界
    for (const auto& f : m_filters) {
        if (f.factorId != factorId || count == 0) continue;
        std::size_t rank = static_cast<std::size_t>(
            f.minPercentile * static_cast<double>(count));
        if (rank >= count) rank = count - 1;
        stats.filterThreshold = sortedValues[rank];
        stats.hasThreshold = true;
    }

    m_snapshot[factorId] = std::move(stats);
}

bool FactorSignalProcessor::passFilter(const std::string& symbol) const
{
    // 交集(AND): 所有因子全部通过才返回 true
    for (const auto& f : m_filters) {
        auto it = m_snapshot.find(f.factorId);
        if (it == m_snapshot.end() || !it->second.hasThreshold) continue;
        const auto& stats = it->second;
        auto vi = stats.values.find(symbol);
        if (vi == stats.values.end()) continue;  // 无该因子数据→不过滤

        if (vi->second < stats.filterThreshold) return false;
    }
    return true;
}

bool FactorSignalProcessor::passAnyFilter(const std::string& symbol) const
{
    // 并集(OR): 任一因子通过即返回 true; 无过滤器也视为通过
    if (m_filters.empty()) return true;
    bool anyPassed = false;
    for (const auto& f : m_filters) {
        auto it = m_snapshot.find(f.factorId);
        if (it == m_snapshot.end() || !it->second.hasThreshold) {
            anyPassed = true;  // 无此因子快照 → 等效通过
            continue;
        }
        const auto& stats = it->second;
        auto vi = stats.values.find(symbol);
        if (vi == stats.values.end()) {
            anyPassed = true;  // 无此标的因子数据 → 等效通过
            continue;
        }
        if (vi->second >= stats.filterThreshold) return true;
    }
    return anyPassed;
}

double FactorSignalProcessor::compositeScore(const std::string& symbol) const
{
    double score = 0.0;
    for (const auto& s : m_scalers) {
        auto it = m_snapshot.find(s.factorId);
        if (it == m_snapshot.end()) return 0.0;
        const auto& stats = it->second;
        auto vi = stats.values.find(symbol);
        if (vi == stats.values.end()) return 0.0;
        if (stats.stdev < 1e-12) continue;

        // Z-score → clamp [-3, 3] → normalize [0, 1]
        double z = (vi->second - stats.mean) / stats.stdev;
        z = std::max(-3.0, std::min(3.0, z));
        double normalized = (z + 3.0) / 6.0;
        double infl = 1.0;
        auto iit = m_factorInfluence.find(s.factorId);
        if (iit != m_factorInfluence.end()) infl = iit->second;
        score += normalized * infl;
    }
    return score;
}

std::vector<std::string> FactorSignalProcessor::rankedSymbols(const std::string& factorId) const
{
    auto it = m_snapshot.find(factorId);
    if (it == m_snapshot.end()) return {};
    const auto& values = it->second.values;
    std::vector<std::pair<std::string, double>> ranked(values.begin(), values.end());
    std::sort(ranked.begin(), ranked.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<std::string> symbols;
    symbols.reserve(ranked.size());
    for (const auto& [sym, _] : ranked) symbols.push_back(sym);
    return symbols;
}

std::vector<std::string> FactorSignalProcessor::allSymbols() const
{
    std::unordered_set<std::string> symbols;
    for (const auto& [fid, snap] : m_snapshot)
        for (const auto& [sym, val] : snap.values)
            symbols.insert(sym);
    return {symbols.begin(), symbols.end()};
}

} // namespace domain::strategy
