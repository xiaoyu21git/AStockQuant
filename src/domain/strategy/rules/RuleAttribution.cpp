// RuleAttribution — 归因收集器实现

#include "RuleAttribution.h"

#include "../../factor/include/factor_compute/IMarketDataView.h"
#include "foundation/log/logging.hpp"

#include <unordered_map>

namespace domain::strategy::rules {

void AttributionCollector::recordBlocked(const BlockedSignalRecord& rec)
{
    m_blockedRecords.push_back(rec);
}

void AttributionCollector::recordExit(const ExitSignalRecord& rec)
{
    m_exitRecords.push_back(rec);
}

void AttributionCollector::compute(const factor::compute::IMarketDataView* view,
                                    int forwardDays)
{
    if (!view) return;

    const auto& dates = view->dates();
    const int totalRows = static_cast<int>(dates.size());
    auto closeMat = view->close();
    const auto& symStrs = view->symbolStrings();

    // 构建 symbol → colIndex 映射 (只做一次)
    std::unordered_map<std::string, int> symToCol;
    for (std::size_t c = 0; c < symStrs.size(); ++c)
        symToCol[symStrs[static_cast<std::size_t>(c)]] = static_cast<int>(c);

    // 按模板聚合归因
    std::map<std::string, RuleAttribution> aggregated;

    // ── 处理被封堵信号: 计算反事实盈亏 ──
    for (const auto& rec : m_blockedRecords) {
        auto colIt = symToCol.find(rec.symbol);
        if (colIt == symToCol.end()) continue;

        const int col = colIt->second;
        const int forwardRow = rec.dayRow + forwardDays;
        if (forwardRow < 0 || forwardRow >= totalRows) continue;

        const double forwardPrice = static_cast<double>(
            closeMat.data[static_cast<std::size_t>(forwardRow) * closeMat.rowStride
                          + static_cast<std::size_t>(col)]);
        if (forwardPrice <= 0.0) continue;

        const double hypotheticalPnL =
            (forwardPrice - rec.price) / rec.price * 100.0;  // 百分比
        const bool wouldWin = hypotheticalPnL > 0.0;

        auto& attr = aggregated[rec.templateId];
        ++attr.preventedTrades;
        attr.preventedHypotheticalPnL += hypotheticalPnL;
        if (wouldWin) attr.preventedWinRate += 1.0;
    }

    // 计算封堵胜率
    for (auto& [tid, attr] : aggregated) {
        if (attr.preventedTrades > 0)
            attr.preventedWinRate /= static_cast<double>(attr.preventedTrades);
    }

    // ── 处理出场信号: 已实现 P&L 直接累加 ──
    for (const auto& rec : m_exitRecords) {
        auto& attr = aggregated[rec.templateId];
        ++attr.triggeredExits;
        attr.exitRealizedPnL += rec.realizedPnL;
    }

    // ── 输出日志 ──
    for (const auto& [tid, attr] : aggregated) {
        INTERNAL_INFO_STREAM << "[Attribution] 模板=" << tid
                             << " 封堵=" << attr.preventedTrades
                             << " 假设盈亏=" << attr.preventedHypotheticalPnL << "%"
                             << " 封堵胜率=" << (attr.preventedWinRate * 100.0) << "%"
                             << " 出场=" << attr.triggeredExits
                             << " 已实现盈亏=" << attr.exitRealizedPnL << "%";
    }
}

std::map<std::string, RuleAttribution> AttributionCollector::results() const
{
    std::map<std::string, RuleAttribution> aggregated;

    for (const auto& rec : m_blockedRecords) {
        auto& attr = aggregated[rec.templateId];
        ++attr.preventedTrades;
    }
    for (const auto& rec : m_exitRecords) {
        auto& attr = aggregated[rec.templateId];
        ++attr.triggeredExits;
        attr.exitRealizedPnL += rec.realizedPnL;
    }
    return aggregated;
}

std::map<std::string, RuleAttribution> AttributionCollector::ruleResults() const
{
    std::map<std::string, RuleAttribution> aggregated;
    for (const auto& rec : m_blockedRecords) {
        auto& attr = aggregated[rec.ruleId.empty() ? rec.templateId : rec.ruleId];
        ++attr.preventedTrades;
    }
    for (const auto& rec : m_exitRecords) {
        auto& attr = aggregated[rec.ruleId.empty() ? rec.templateId : rec.ruleId];
        ++attr.triggeredExits;
        attr.exitRealizedPnL += rec.realizedPnL;
    }
    return aggregated;
}

void AttributionCollector::clear()
{
    m_blockedRecords.clear();
    m_exitRecords.clear();
}

} // namespace domain::strategy::rules
