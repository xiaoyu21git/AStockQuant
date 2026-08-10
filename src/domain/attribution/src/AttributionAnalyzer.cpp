// AttributionAnalyzer.cpp — 绩效归因分析器实现
#include "AttributionAnalyzer.h"

#include <foundation/log/logging.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace domain::attribution {

// ═══════════════════════════════════════════════════════════════════════════
// 构造
// ═══════════════════════════════════════════════════════════════════════════

AttributionAnalyzer::AttributionAnalyzer(Config config)
    : m_config(std::move(config))
{
}

// ═══════════════════════════════════════════════════════════════════════════
// 主入口
// ═══════════════════════════════════════════════════════════════════════════

AttributionReport AttributionAnalyzer::analyze(
    const domain::strategy::StrategyBacktestResult& result) const
{
    if (!result.success || result.tradeLog.empty()) {
        INTERNAL_INFO_STREAM << "[AttributionAnalyzer] 无可用数据: success="
                            << result.success << " tradeLog.size=" << result.tradeLog.size();
        return AttributionReport{};
    }

    // 计算总盈亏 (已实现部分)
    double totalPnl = 0.0;
    for (const auto& t : result.tradeLog) {
        totalPnl += std::abs(t.realizedPnl);
    }
    // 总 PnL 可能为负/零, 用于计算百分比贡献时取绝对值之和
    double absPnlSum = totalPnl > 0.0 ? totalPnl : 1.0;

    AttributionReport report;

    // ── 板块归因 ──
    report.sectorBreakdown = computeSectorAttribution(result.tradeLog, absPnlSum);

    // ── 因子归因 ──
    int totalDays = result.timeSeries.dates.empty()
        ? 1
        : static_cast<int>(result.timeSeries.dates.size());
    report.factorBreakdown = computeFactorAttribution(
        result.hybridFactorCoverage, result.rankIC, totalDays);

    // ── 择时归因 ──
    report.timingBreakdown = computeTimingAttribution(
        report.sectorBreakdown,
        result.metrics.totalReturn,
        result.metrics.alpha); // benchmarkReturn 用 alpha 近似
                               // (alpha = portfolioReturn - beta × benchmarkReturn)

    report.isValid = true;

    INTERNAL_INFO_STREAM << "[AttributionAnalyzer] 报告已生成: "
                         << report.sectorBreakdown.size() << " sectors, "
                         << report.factorBreakdown.size() << " factors, "
                         << "timing simplified=" << report.timingBreakdown.isSimplified;

    return report;
}

// ═══════════════════════════════════════════════════════════════════════════
// 板块归因
// ═══════════════════════════════════════════════════════════════════════════

std::vector<SectorAttribution> AttributionAnalyzer::computeSectorAttribution(
    const std::vector<domain::strategy::BacktestTradeRecord>& tradeLog,
    double absPnlSum) const
{
    // 按行业聚合
    struct SectorAgg {
        double realizedPnl{0.0};
        int tradeCount{0};
        std::unordered_set<std::string> symbols;
    };
    std::unordered_map<std::string, SectorAgg> sectorMap;

    // 记录每只股票的总买入金额 (用于计算权重)
    std::unordered_map<std::string, double> stockBuyAmount;
    std::unordered_map<std::string, std::string> stockSector;

    for (const auto& t : tradeLog) {
        std::string sector = "未分类";
        if (m_config.sectorLookup) {
            sector = m_config.sectorLookup(t.symbol);
            if (sector.empty()) sector = "未分类";
        }

        auto& agg = sectorMap[sector];
        agg.realizedPnl += t.realizedPnl;
        ++agg.tradeCount;
        agg.symbols.insert(t.symbol);

        // 记录买入金额用于权重计算
        if (t.isBuy) {
            stockBuyAmount[t.symbol] += t.quantity * t.price;
            stockSector[t.symbol] = sector;
        }
    }

    if (sectorMap.empty()) return {};

    // 计算总买入金额 (用于权重归一化)
    double totalBuyAmount = 0.0;
    for (const auto& [sym, amt] : stockBuyAmount) {
        totalBuyAmount += amt;
    }
    if (totalBuyAmount <= 0.0) totalBuyAmount = 1.0;

    // 转换为 SectorAttribution 向量
    std::vector<SectorAttribution> result;
    result.reserve(sectorMap.size());

    for (auto& [sectorName, agg] : sectorMap) {
        SectorAttribution sa;
        sa.sectorName = sectorName;
        sa.totalRealizedPnl = agg.realizedPnl;
        sa.tradeCount = agg.tradeCount;
        sa.stockCount = static_cast<int>(agg.symbols.size());
        sa.returnContribution = (absPnlSum > 0.0)
            ? (agg.realizedPnl / absPnlSum) * 100.0
            : 0.0;

        // 计算该行业的组合权重和收益率
        double sectorBuyAmount = 0.0;
        for (const auto& sym : agg.symbols) {
            auto it = stockBuyAmount.find(sym);
            if (it != stockBuyAmount.end()) {
                sectorBuyAmount += it->second;
            }
        }
        sa.averageWeight = sectorBuyAmount / totalBuyAmount;

        // 行业收益率: PnL / 投入, 防除零
        sa.portfolioReturn = (sectorBuyAmount > 0.0)
            ? (agg.realizedPnl / sectorBuyAmount) * 100.0
            : 0.0;

        result.push_back(std::move(sa));
    }

    // 按贡献降序排列
    std::sort(result.begin(), result.end(),
              [](const SectorAttribution& a, const SectorAttribution& b) {
                  return a.returnContribution > b.returnContribution;
              });

    INTERNAL_INFO_STREAM << "[AttributionAnalyzer] 行业归因: "
                         << result.size() << " sectors from "
                         << tradeLog.size() << " trades";

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// 因子归因
// ═══════════════════════════════════════════════════════════════════════════

std::vector<FactorAttribution> AttributionAnalyzer::computeFactorAttribution(
    const std::vector<domain::strategy::HybridFactorCoverage>& factorCoverage,
    double rankIC,
    int totalDays) const
{
    if (factorCoverage.empty()) return {};

    std::vector<FactorAttribution> result;
    result.reserve(factorCoverage.size());

    for (const auto& fc : factorCoverage) {
        FactorAttribution fa;
        fa.factorId = fc.factorId;

        // 查询因子权重 (Bridge 层注入)
        if (m_config.factorWeightLookup) {
            fa.factorWeight = m_config.factorWeightLookup(fc.factorId);
        }

        fa.factorIC = rankIC;
        fa.coveredDays = fc.coveredDays;

        // 近似估算: contribution = weight × IC × coverage
        // 假设: 因子回报率恒定且因子间独立 (真实场景不满足, 仅供粗参考)
        double coverage = (totalDays > 0)
            ? static_cast<double>(fc.coveredDays) / totalDays
            : 0.0;
        fa.estimatedContribution = fa.factorWeight * rankIC * coverage;

        fa.estimationMethod = FactorEstimationMethod::Approximate;

        result.push_back(std::move(fa));
    }

    // 按贡献降序排列
    std::sort(result.begin(), result.end(),
              [](const FactorAttribution& a, const FactorAttribution& b) {
                  return a.estimatedContribution > b.estimatedContribution;
              });

    INTERNAL_INFO_STREAM << "[AttributionAnalyzer] 因子归因: "
                         << result.size() << " factors, rankIC=" << rankIC
                         << " method=approximate";

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Brinson 择时归因
// ═══════════════════════════════════════════════════════════════════════════

TimingAttribution AttributionAnalyzer::computeTimingAttribution(
    const std::vector<SectorAttribution>& sectorData,
    double portfolioReturn,
    double benchmarkReturn) const
{
    TimingAttribution ta;
    ta.portfolioReturn = portfolioReturn;
    ta.benchmarkReturn = benchmarkReturn;

    // Phase 1: 若无 benchmark 行业数据, 使用简化模式
    if (!m_config.benchmarkLookup) {
        ta.isSimplified = true;
        ta.excessReturn = portfolioReturn - benchmarkReturn;
        ta.allocationEffect = 0.0;
        ta.selectionEffect = 0.0;
        ta.interactionEffect = 0.0;

        INTERNAL_INFO_STREAM << "[AttributionAnalyzer] 择时归因 (简化): "
                             << "excessReturn=" << ta.excessReturn
                             << " (no benchmark sector data)";
        return ta;
    }

    // Phase 2: 完整 Brinson (当前未实现)
    // TODO: 对接 benchmarkLookup 回调获取行业权重/收益率
    ta.isSimplified = true;
    ta.excessReturn = portfolioReturn - benchmarkReturn;
    ta.allocationEffect = 0.0;
    ta.selectionEffect = 0.0;
    ta.interactionEffect = 0.0;

    INTERNAL_INFO_STREAM << "[AttributionAnalyzer] 择时归因: 基准行业数据 "
                         << "available but Brinson fully integrated is Phase 2, "
                         << "using simplified mode";

    return ta;
}

} // namespace domain::attribution
