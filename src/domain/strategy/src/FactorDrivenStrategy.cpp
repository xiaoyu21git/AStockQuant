#include "FactorDrivenStrategy.h"

#include <algorithm>
#include <cmath>

namespace domain::strategy {

FactorDrivenStrategy::FactorDrivenStrategy(
    StrategyInstanceId instanceId, const Config& cfg,
    std::shared_ptr<const FactorSignalProcessor> processor)
    : m_instanceId(instanceId), m_cfg(cfg), m_processor(std::move(processor)) {}

void FactorDrivenStrategy::evaluate(
    const std::vector<RuntimeFactorSnapshot>& /*factorSnapshots*/,
    const RuntimeStrategyContext& ctx,
    std::vector<StrategySignal>& out)
{
    if (!m_processor || !m_processor->enabled()) return;

    // 从候选池获取所有标的 + 计算综合得分
    auto symbols = m_processor->allSymbols();

    // 按综合得分排序 (降序)
    std::vector<std::pair<std::string, double>> ranked;
    ranked.reserve(symbols.size());
    for (const auto& sym : symbols) {
        double score = m_processor->compositeScore(sym);
        if (score > 0.0) ranked.emplace_back(sym, score);
    }
    std::sort(ranked.begin(), ranked.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    // Top-N 选股: 得分 > entryThreshold 的买入
    int selected = 0;
    for (const auto& [sym, score] : ranked) {
        if (selected >= m_cfg.topN) break;

        bool isBuy = score >= m_cfg.entryScoreThreshold;
        double weight = 0.0;

        if (isBuy) {
            // 因子得分 → 仓位权重 (得分越高仓位越重, 单票上限 5%)
            weight = m_cfg.minWeightPerStock
                     + (m_cfg.maxWeightPerStock - m_cfg.minWeightPerStock)
                     * std::clamp((score - m_cfg.entryScoreThreshold)
                                  / (1.0 - m_cfg.entryScoreThreshold), 0.0, 1.0);
        } else if (score < m_cfg.exitScoreThreshold) {
            // 得分过低 → 卖出信号
            isBuy = false;
            weight = 0.0;
        } else {
            continue;  // 中间地带: 不产生信号
        }

        out.push_back(StrategySignal(
            ctx.strategyInstanceId(),
            InstrumentId{0},  // 由 OrderGenerator 填充真实 instrumentId
            isBuy ? RuntimeOrderSide::Buy : RuntimeOrderSide::Sell,
            score,
            weight,
            sym));
        ++selected;
    }
}

std::shared_ptr<IRuntimeStrategy> FactorDrivenStrategy::create(
    StrategyInstanceId instanceId, const StrategyCreationParams& params)
{
    Config cfg;
    cfg.topN              = params.topN > 0 ? params.topN : 50;
    cfg.maxWeightPerStock = params.maxWeightPerStock > 0 ? params.maxWeightPerStock : 0.05;
    cfg.minWeightPerStock = params.minWeightPerStock > 0 ? params.minWeightPerStock : 0.01;
    cfg.entryScoreThreshold = 0.6;
    cfg.exitScoreThreshold  = 0.3;

    return std::make_shared<FactorDrivenStrategy>(instanceId, cfg, nullptr);
}

} // namespace domain::strategy
