#include "FactorSignalProcessor.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace domain::strategy {

void FactorSignalProcessor::updateSnapshot(
    const std::unordered_map<std::string, double>& factorValues)
{
    // 每个因子独立存储一份 symbol→value 快照
    for (const auto& f : m_filters) {
        auto& snap = m_snapshot[f.factorId];
        snap.clear();
        for (const auto& [sym, val] : factorValues)
            if (std::isfinite(val)) snap[sym] = val;
    }
    for (const auto& s : m_scalers) {
        auto& snap = m_snapshot[s.factorId];
        snap.clear();
        for (const auto& [sym, val] : factorValues)
            if (std::isfinite(val)) snap[sym] = val;
    }
}

bool FactorSignalProcessor::passFilter(const std::string& symbol) const
{
    for (const auto& f : m_filters) {
        auto it = m_snapshot.find(f.factorId);
        if (it == m_snapshot.end()) continue;
        const auto& snap = it->second;
        auto vi = snap.find(symbol);
        if (vi == snap.end()) continue;  // 无该因子数据→不过滤

        // 计算该符号在所有符号中的分位数
        std::vector<double> vals; vals.reserve(snap.size());
        for (const auto& [_, v] : snap) vals.push_back(v);
        std::sort(vals.begin(), vals.end());
        size_t rank = 0;
        for (size_t i = 0; i < vals.size(); ++i) {
            if (vals[i] >= vi->second) { rank = i; break; }
        }
        double pct = static_cast<double>(rank) / static_cast<double>(vals.size());
        if (pct < f.minPercentile) return false;
    }
    return true;
}

double FactorSignalProcessor::scaleFactor(const std::string& symbol) const
{
    double result = 1.0;
    for (const auto& s : m_scalers) {
        auto it = m_snapshot.find(s.factorId);
        if (it == m_snapshot.end()) continue;
        const auto& snap = it->second;
        auto vi = snap.find(symbol);
        if (vi == snap.end()) continue;

        // Z-score → clamp to [-3,3] → map to [0.5, 1.5]
        // 先算均值和标准差
        double sum = 0, sq = 0; size_t n = 0;
        for (const auto& [_, v] : snap) { sum += v; sq += v*v; ++n; }
        if (n < 2) continue;
        double mean = sum / static_cast<double>(n);
        double stdev = std::sqrt(sq / static_cast<double>(n) - mean * mean);
        if (stdev < 1e-12) continue;

        double z = (vi->second - mean) / stdev;
        z = std::max(-3.0, std::min(3.0, z));
        double mapped = 0.5 + (z + 3.0) / 6.0 * (1.5 - 0.5); // [-3,3]→[0.5,1.5]
        double weighted = 1.0 + (mapped - 1.0) * s.influence;   // 影响力系数
        result *= std::max(0.1, weighted);                       // 不低于0.1
    }
    return result;
}

} // namespace domain::strategy
