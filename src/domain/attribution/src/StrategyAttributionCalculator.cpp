#include "StrategyAttributionCalculator.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace domain::attribution {

bool StrategyAttributionCalculator::isUnclassifiedCode(const std::string& code)
{
    return code == kUnclassifiedCode;
}

double StrategyAttributionCalculator::valueOrZero(const std::map<std::string, double>& table,
                                                  const std::string& code)
{
    const auto it = table.find(code);
    return it == table.end() ? 0.0 : it->second;
}

std::string StrategyAttributionCalculator::resolveSectorCode(const Inputs& inputs,
                                                             const std::string& symbol) const
{
    if (!inputs.symbolSector) return kUnclassifiedCode;
    const std::string* code = inputs.symbolSector(symbol);
    if (code == nullptr || code->empty()) return kUnclassifiedCode;
    return *code;
}

std::string StrategyAttributionCalculator::resolveSectorName(const Inputs& inputs,
                                                             const std::string& code) const
{
    if (isUnclassifiedCode(code)) return kUnclassifiedName;
    if (!inputs.sectorName) return std::string();
    const std::string* name = inputs.sectorName(code);
    return name == nullptr ? std::string() : *name;
}

StrategyAttributionReport StrategyAttributionCalculator::compute(const Inputs& inputs) const
{
    StrategyAttributionReport report;

    if (inputs.tradeLog == nullptr || inputs.snapshots == nullptr || inputs.snapshots->empty()) {
        report.isValid = false;
        report.notice = "归因输入缺失（成交明细或持仓快照为空），无法计算。";
        return report;
    }

    computeStocks(*inputs.tradeLog, report);

    const std::vector<DayContext> days = buildAlignedDays(inputs);
    computeSectors(inputs, days, report);
    computeBrinson(days, report.brinson);

    report.brinson.totalDays = inputs.backtestDays.empty()
        ? static_cast<int>(inputs.snapshots->size())
        : static_cast<int>(inputs.backtestDays.size());
    report.brinson.maxBenchmarkGapDays = computeMaxBenchmarkGapDays(inputs);
    report.brinson.lowDataQuality =
        report.brinson.totalDays > 0 &&
        report.brinson.maxBenchmarkGapDays > static_cast<int>(report.brinson.totalDays * 0.2);

    // ── 口径/降级说明 ──
    std::string notice = "Brinson 分解覆盖 " + std::to_string(report.brinson.alignedDays) +
        " 个对齐交易日（共 " + std::to_string(report.brinson.totalDays) +
        " 个回测日），基准数据最大无覆盖间隔 " +
        std::to_string(report.brinson.maxBenchmarkGapDays) + " 天。";
    if (report.brinson.lowDataQuality) {
        notice += " 基准行业数据覆盖不足，Brinson 分解结果仅供参考。";
    }
    if (report.brinson.alignedDays == 0) {
        notice += " 组合快照与基准数据无对齐日，Brinson 不可计算。";
    }
    if (!report.hasSectorNames) {
        notice += " 行业名称表为空，仅显示行业代码（运行 tools/import_sw_industry.py 可导入行业名）。";
    }
    notice += " 口径：行业/个股为已实现盈亏（元），Brinson 为对齐日重构毛收益（不含现金），"
              "与净值收益统计存在口径差异属预期。";
    report.notice = std::move(notice);

    report.isValid = true;
    return report;
}

void StrategyAttributionCalculator::computeStocks(
    const std::vector<domain::strategy::BacktestTradeRecord>& tradeLog,
    StrategyAttributionReport& report) const
{
    std::map<std::string, StockAttributionItem> stockMap;
    for (const auto& trade : tradeLog) {
        auto& item = stockMap[trade.symbol];
        item.symbol = trade.symbol;
        if (trade.isBuy) {
            ++item.buyCount;
        } else {
            ++item.sellCount;
            item.realizedPnl += trade.realizedPnl;
            if (trade.realizedPnl > 0.0) ++item.winCount;
        }
    }
    report.stocks.reserve(stockMap.size());
    for (auto& kv : stockMap) {
        report.stocks.push_back(std::move(kv.second));
    }
    std::sort(report.stocks.begin(), report.stocks.end(),
              [](const StockAttributionItem& a, const StockAttributionItem& b) {
                  return a.realizedPnl > b.realizedPnl;
              });
}

void StrategyAttributionCalculator::computeSectors(const Inputs& inputs,
                                                   const std::vector<DayContext>& days,
                                                   StrategyAttributionReport& report) const
{
    // ── 1. tradeLog 按行业聚合 ──
    std::map<std::string, SectorAgg> agg;
    for (const auto& trade : *inputs.tradeLog) {
        const std::string code = resolveSectorCode(inputs, trade.symbol);
        auto& item = agg[code];
        ++item.tradeCount;
        item.symbols.insert(trade.symbol);
        if (trade.isBuy) {
            item.buyAmount += trade.quantity * trade.price;
        } else {
            item.totalPnl += trade.realizedPnl;
        }
    }

    // ── 2. 组合/基准权重逐日累加（对齐日平均） ──
    std::map<std::string, double> portfolioWeightSum;   // 行业 ← Σ_日 Σ mktval/equity
    std::map<std::string, double> benchmarkWeightSum;   // 行业 ← Σ_日 Σ w_b
    for (const auto& day : days) {
        for (const auto& kv : day.rawPortfolioWeight) {
            portfolioWeightSum[kv.first] += kv.second;
        }
        for (const auto& kv : day.benchmarkWeight) {
            benchmarkWeightSum[kv.first] += kv.second;
        }
    }

    const double dayCount = static_cast<double>(days.empty() ? 0 : days.size());
    double totalAbsPnl = 0.0;
    for (const auto& [code, item] : agg) {
        totalAbsPnl += std::abs(item.totalPnl);
    }

    report.hasSectorNames = false;
    report.sectors.reserve(agg.size());
    for (auto& [code, item] : agg) {
        SectorAttributionItem out;
        out.sectorCode = code;
        out.sectorName = resolveSectorName(inputs, code);
        out.totalRealizedPnl = item.totalPnl;
        out.buyAmount = item.buyAmount;
        out.realizedReturn = item.buyAmount > 0.0 ? item.totalPnl / item.buyAmount : 0.0;
        out.averageWeight = dayCount > 0.0 ? portfolioWeightSum[code] / dayCount : 0.0;
        out.benchmarkWeight = dayCount > 0.0 ? benchmarkWeightSum[code] / dayCount : 0.0;
        out.returnContribution =
            totalAbsPnl > 0.0 ? item.totalPnl / totalAbsPnl : 0.0;
        out.tradeCount = item.tradeCount;
        out.stockCount = static_cast<int>(item.symbols.size());
        if (!isUnclassifiedCode(code) && !out.sectorName.empty()) {
            report.hasSectorNames = true;
        }
        report.sectors.push_back(std::move(out));
    }
    // |贡献| 降序
    std::sort(report.sectors.begin(), report.sectors.end(),
              [](const SectorAttributionItem& a, const SectorAttributionItem& b) {
                  return std::abs(a.returnContribution) > std::abs(b.returnContribution);
              });
}

std::vector<StrategyAttributionCalculator::DayContext>
StrategyAttributionCalculator::buildAlignedDays(const Inputs& inputs) const
{
    std::vector<DayContext> days;
    if (inputs.view == nullptr) return days;

    const auto& viewDates = inputs.view->dates();
    const auto& symbols = inputs.view->symbolStrings();
    const auto closeMat = inputs.view->close();
    if (!closeMat.isValid() || viewDates.empty() || symbols.empty()) return days;

    // date → 行号 / symbol → 列号
    std::map<domain::DomainDate, int> dateRows;
    for (std::size_t r = 0; r < viewDates.size(); ++r) {
        dateRows[viewDates[r]] = static_cast<int>(r);
    }
    std::unordered_map<std::string, int> symbolCols;
    symbolCols.reserve(symbols.size());
    for (std::size_t c = 0; c < symbols.size(); ++c) {
        symbolCols[symbols[c]] = static_cast<int>(c);
    }

    // 快照日索引（保序）
    std::map<domain::DomainDate, const PositionSnapshotCollector::DaySnapshot*> snapshotByDate;
    for (const auto& snap : *inputs.snapshots) {
        snapshotByDate[snap.date] = &snap;
    }

    for (const auto& [date, benchWeights] : inputs.benchmarkStockWeights) {
        const auto itSnap = snapshotByDate.find(date);
        if (itSnap == snapshotByDate.end()) continue;  // 非组合快照日 → 不在对齐集合
        const auto itRow = dateRows.find(date);
        if (itRow == dateRows.end() || itRow->second + 1 >= static_cast<int>(viewDates.size())) {
            continue;  // 无 t / 无 t+1 行情 → 剔除
        }

        DayContext day;
        day.date = date;
        day.rowToday = itRow->second;
        day.rowNext = day.rowToday + 1;

        const auto& snap = *itSnap->second;
        // isfinite 守卫: 防 NaN 穿透 (NaN <= 0.0 为 false 会漏过 <= 判断)
        if (!std::isfinite(snap.equity) || snap.equity <= 0.0) continue;

        // 组合侧：逐持仓 w_p(i) = mktval/equity，停牌缺价剔除
        for (const auto& entry : snap.entries) {
            const auto itCol = symbolCols.find(entry.symbol);
            if (itCol == symbolCols.end()) continue;
            const double c0 = closeMat.data[day.rowToday * closeMat.rowStride + itCol->second];
            const double c1 = closeMat.data[day.rowNext * closeMat.rowStride + itCol->second];
            if (!std::isfinite(c0) || c0 <= 0.0 || !std::isfinite(c1) || c1 <= 0.0) continue;
            if (!std::isfinite(entry.marketValue)) continue;
            const double wp = entry.marketValue / snap.equity;
            if (!std::isfinite(wp) || wp < 0.0) continue;
            const double ret = c1 / c0 - 1.0;
            const std::string code = resolveSectorCode(inputs, entry.symbol);
            day.rawPortfolioWeight[code] += wp;
            day.portfolioWeight[code] += wp;
            day.portfolioWeightedReturn[code] += wp * ret;
            day.portfolioTotalWeight += wp;
        }

        // 基准侧：成分权重 w_b(i)，停牌缺价剔除归一化
        for (const auto& [sym, wb] : *benchWeights) {
            const auto itCol = symbolCols.find(sym);
            if (itCol == symbolCols.end()) continue;
            const double c0 = closeMat.data[day.rowToday * closeMat.rowStride + itCol->second];
            const double c1 = closeMat.data[day.rowNext * closeMat.rowStride + itCol->second];
            if (!std::isfinite(c0) || c0 <= 0.0 || !std::isfinite(c1) || c1 <= 0.0) continue;
            if (!std::isfinite(wb) || wb < 0.0) continue;
            const double ret = c1 / c0 - 1.0;
            const std::string code = resolveSectorCode(inputs, sym);
            day.benchmarkWeight[code] += wb;
            day.benchmarkWeightedReturn[code] += wb * ret;
            day.benchmarkTotalWeight += wb;
        }

        if (!std::isfinite(day.portfolioTotalWeight) || day.portfolioTotalWeight <= 0.0
            || !std::isfinite(day.benchmarkTotalWeight) || day.benchmarkTotalWeight <= 0.0) continue;
        days.push_back(std::move(day));
    }
    return days;
}

StrategyAttributionCalculator::DayBrinson
StrategyAttributionCalculator::computeDayBrinson(const DayContext& day) const
{
    // 行业并集：组合持仓 ∪ 基准成分；缺失侧权重/收益按 0 处理（代数恒等式保持）
    std::set<std::string> sectorSet;
    for (const auto& [code, w] : day.portfolioWeight) sectorSet.insert(code);
    for (const auto& [code, w] : day.benchmarkWeight) sectorSet.insert(code);

    DayBrinson out;
    for (const auto& code : sectorSet) {
        const double wpSum = valueOrZero(day.portfolioWeight, code);
        const double wbSum = valueOrZero(day.benchmarkWeight, code);
        const double wp = wpSum / day.portfolioTotalWeight;
        const double wb = wbSum / day.benchmarkTotalWeight;
        const double rp = wpSum > 0.0
            ? valueOrZero(day.portfolioWeightedReturn, code) / wpSum : 0.0;
        const double rb = wbSum > 0.0
            ? valueOrZero(day.benchmarkWeightedReturn, code) / wbSum : 0.0;
        out.ae += (wp - wb) * rb;
        out.se += wb * (rp - rb);
        out.ie += (wp - wb) * (rp - rb);
        out.portfolioReturn += wp * rp;
        out.benchmarkReturn += wb * rb;
        out.excess += wp * rp - wb * rb;
    }
    return out;
}

void StrategyAttributionCalculator::computeBrinson(const std::vector<DayContext>& days,
                                                   BrinsonBreakdown& out) const
{
    out.alignedDays = static_cast<int>(days.size());
    out.cumulativeAe.reserve(days.size());
    out.cumulativeSe.reserve(days.size());
    out.cumulativeIe.reserve(days.size());

    std::map<int, BrinsonBreakdown::YearBreak> yearlyMap;
    for (const auto& day : days) {
        const DayBrinson daily = computeDayBrinson(day);
        out.allocationEffect += daily.ae;
        out.selectionEffect += daily.se;
        out.interactionEffect += daily.ie;
        out.excessReturn += daily.excess;
        out.portfolioReturn += daily.portfolioReturn;
        out.benchmarkReturn += daily.benchmarkReturn;
        out.cumulativeAe.push_back(out.allocationEffect);
        out.cumulativeSe.push_back(out.selectionEffect);
        out.cumulativeIe.push_back(out.interactionEffect);

        auto& yb = yearlyMap[day.date.value / 10000];
        yb.year = day.date.value / 10000;
        yb.ae += daily.ae;
        yb.se += daily.se;
        yb.ie += daily.ie;
        yb.excess += daily.excess;
    }
    // 恒等式（对齐口径）：|AE+SE+IE − Σ_{aligned}(R_p−R_b)|
    out.identityError = std::abs(out.allocationEffect + out.selectionEffect +
                                 out.interactionEffect - out.excessReturn);

    for (auto& kv : yearlyMap) {
        out.yearly.push_back(std::move(kv.second));
    }
}

int StrategyAttributionCalculator::computeMaxBenchmarkGapDays(const Inputs& inputs) const
{
    const auto& days = inputs.backtestDays;
    if (days.empty()) return 0;
    const auto& bench = inputs.benchmarkStockWeights;
    if (bench.empty()) return static_cast<int>(days.size());

    int maxRun = 0;
    int run = 0;
    for (const auto& d : days) {
        if (bench.find(d) == bench.end()) {
            ++run;
            maxRun = std::max(maxRun, run);
        } else {
            run = 0;
        }
    }
    return maxRun;
}

} // namespace domain::attribution
