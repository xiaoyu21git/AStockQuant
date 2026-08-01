#include "../include/MultiFactorStrategy.h"

#include "MarketDataService.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <utility>

#include "foundation/log/logging.hpp"

namespace {

// ── 常量 ──
constexpr double kZeroValue = 0.0;
constexpr double kFullWeight = 1.0;
constexpr double kEpsilon = 1e-12;
constexpr double kMaxZScore = 3.0;
constexpr double kMinZScore = -3.0;
constexpr int kVolatilityLookbackBars = 20;
constexpr int kMaxDiagLogs = 3;
constexpr const char* kMarketCapFieldName = "market_cap";

} // anonymous namespace

namespace domain::strategy {

// ══════════════════════════════════════════════════════════════════════════════
// 构造 / 接口
// ══════════════════════════════════════════════════════════════════════════════

MultiFactorStrategy::MultiFactorStrategy(
    StrategyInstanceId instanceId, MultiFactorConfig config)
    : m_instanceId(instanceId)
    , m_config(std::move(config))
{
}

StrategyInstanceId MultiFactorStrategy::instanceId() const noexcept
{
    return m_instanceId;
}

bool MultiFactorStrategy::isEnabled() const noexcept
{
    return m_config.isValid();
}

rules::RuleSetId MultiFactorStrategy::ruleSetId() const noexcept
{
    return rules::kRuleSetAllPass;
}

bool MultiFactorStrategy::usesFactors() const noexcept
{
    return true;
}

// ══════════════════════════════════════════════════════════════════════════════
// evaluate — 主入口
// ══════════════════════════════════════════════════════════════════════════════

void MultiFactorStrategy::evaluate(
    const std::vector<RuntimeFactorSnapshot>& factorSnapshots,
    const RuntimeStrategyContext& context,
    std::vector<StrategySignal>& outputSignals)
{
    // ── 防御检查 ──
    if (!m_config.isValid() || !context.isValid()
        || context.strategyInstanceId() != m_instanceId
        || factorSnapshots.empty()) {
        static int diagCount = 0;
        if (++diagCount <= kMaxDiagLogs) {
            std::string reason;
            if (!m_config.isValid()) reason = "!config.isValid";
            else if (!context.isValid()) reason = "!context.isValid";
            else if (context.strategyInstanceId() != m_instanceId)
                reason = "instanceId mismatch";
            else if (factorSnapshots.empty()) reason = "factorSnapshots empty";
            INTERNAL_WARN_STREAM << "[MultiFactor] evaluate skipped: " << reason
                << " snapshots=" << factorSnapshots.size()
                << " config.valid=" << m_config.isValid();
        }
        return;
    }

    // ── 解析行情视图（用于 symbol 映射 + 权重方案数据）──
    const auto* view = resolveMarketView(context);
    if (!view) {
        static int noViewDiag = 0;
        if (++noViewDiag <= kMaxDiagLogs) {
            INTERNAL_WARN_STREAM << "[MultiFactor] evaluate skipped: no market view";
        }
        return;
    }

    // ── 阶段1: 截面 Z-score 归一化 ──
    auto normalized = normalizeCrossSectional(factorSnapshots);
    if (normalized.empty()) return;

    // ── 阶段2: 加权合成评分 ──
    auto allScores = computeCompositeScores(normalized);
    if (allScores.empty()) return;

    static int scoreDiag = 0;
    if (++scoreDiag <= kMaxDiagLogs) {
        double maxScore = kZeroValue;
        for (auto& s : allScores) {
            if (s.compositeScore > maxScore) maxScore = s.compositeScore;
        }
        INTERNAL_INFO_STREAM << "[MultiFactor] scores=" << allScores.size()
            << " maxCompositeScore=" << maxScore;
    }

    // ── 构建 id→fullSymbol 映射 ──
    auto idToSymbol = buildIdToSymbolMap(view);

    // ── 先做全量排名（降序），用于换仓判断 ──
    std::sort(allScores.begin(), allScores.end(),
        [](const CompositeRecord& a, const CompositeRecord& b) {
            return a.compositeScore > b.compositeScore;
        });

    // 构建 symbolId → 排名 映射（排名从 1 开始）
    std::unordered_map<std::uint32_t, int> rankById;
    rankById.reserve(allScores.size());
    for (std::size_t i = 0; i < allScores.size(); ++i) {
        rankById[allScores[i].symbolId] = static_cast<int>(i + 1);
    }

    // ── 阶段3: 卖出信号 + 阶段4-6: 买入信号 → 全部暂存到 temp ──
    std::vector<StrategySignal> tempSignals;
    tempSignals.reserve(outputSignals.size() + m_config.topN + 64);

    emitExitSignals(allScores, rankById, context, view, tempSignals);

    auto selected = rankAndSelect(allScores);
    auto weights = allocateWeights(selected, context, view);
    emitBuySignals(selected, weights, idToSymbol, context, tempSignals);

    // ── 排序 + 硬上限 (与 StrategyBase 一致): 卖出优先, 买入按分数降序, 限 maxPositions ──
    std::sort(tempSignals.begin(), tempSignals.end(),
        [](const StrategySignal& a, const StrategySignal& b) {
            if (a.side() == RuntimeOrderSide::Sell && b.side() == RuntimeOrderSide::Buy) return true;
            if (a.side() == RuntimeOrderSide::Buy  && b.side() == RuntimeOrderSide::Sell) return false;
            return a.score() > b.score();
        });

    int buyCount = 0;
    const int buyLimit = std::max(1, m_config.maxPositions);
    outputSignals.reserve(outputSignals.size() + tempSignals.size());
    for (auto& sig : tempSignals) {
        if (sig.side() == RuntimeOrderSide::Buy) {
            if (buyCount++ >= buyLimit) continue;
        }
        outputSignals.push_back(std::move(sig));
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// 阶段1: 截面 Z-score 归一化
// ══════════════════════════════════════════════════════════════════════════════

std::vector<MultiFactorStrategy::SymbolFactorScore>
MultiFactorStrategy::normalizeCrossSectional(
    const std::vector<RuntimeFactorSnapshot>& snapshots) const
{
    // ── 第一遍: 按因子收集统计量（sum, sumSq, count）──
    std::unordered_map<std::string, FactorStats> statsByFactor;
    statsByFactor.reserve(m_config.factorIds.size());

    // 同时构建 (factorId, symbolId) → value 索引，避免第二遍再次遍历
    // key = factorId + ":" + symbolId（用 composite key 简化为两个 map）
    // 改为: symbolId → {factorId → value}
    std::unordered_map<std::uint32_t,
        std::unordered_map<std::string, double>> valuesBySymbol;

    // ── 诊断: 原始因子值统计 ──
    {
        std::unordered_map<std::string, double> minByFactor, maxByFactor;
        std::unordered_map<std::string, int> countByFactor, nonZeroByFactor;
        for (const auto& snap : snapshots) {
            if (snap.symbolId == 0 || snap.factorId.empty()) continue;
            if (m_config.weights.find(snap.factorId) == m_config.weights.end()) continue;
            auto& mn = minByFactor[snap.factorId];
            auto& mx = maxByFactor[snap.factorId];
            if (countByFactor[snap.factorId] == 0 || snap.factorValue < mn)
                mn = snap.factorValue;
            if (countByFactor[snap.factorId] == 0 || snap.factorValue > mx)
                mx = snap.factorValue;
            ++countByFactor[snap.factorId];
            if (std::abs(snap.factorValue) > kEpsilon)
                ++nonZeroByFactor[snap.factorId];
        }
        static int rawDiag = 0;
        if (++rawDiag <= kMaxDiagLogs) {
            for (const auto& fid : m_config.factorIds) {
                INTERNAL_INFO_STREAM << "[MultiFactor] rawFactor: fid=" << fid
                    << " count=" << countByFactor[fid]
                    << " nonZero=" << nonZeroByFactor[fid]
                    << " min=" << minByFactor[fid]
                    << " max=" << maxByFactor[fid];
            }
        }
    }

    for (const auto& snap : snapshots) {
        if (snap.symbolId == 0 || snap.factorId.empty()) continue;
        // 只处理配置中声明的因子
        if (m_config.weights.find(snap.factorId) == m_config.weights.end())
            continue;

        auto& stats = statsByFactor[snap.factorId];
        stats.sum += snap.factorValue;
        stats.sumSquares += snap.factorValue * snap.factorValue;
        ++stats.count;

        valuesBySymbol[snap.symbolId][snap.factorId] = snap.factorValue;
    }

    // ── 计算每个因子的 mean 和 inverseStdDev ──
    std::unordered_map<std::string, double> meanByFactor;
    std::unordered_map<std::string, double> invStdByFactor;
    meanByFactor.reserve(statsByFactor.size());
    invStdByFactor.reserve(statsByFactor.size());

    for (const auto& [fid, stats] : statsByFactor) {
        if (stats.count == 0) continue;
        const double mean = stats.sum / static_cast<double>(stats.count);
        const double variance =
            (stats.sumSquares / static_cast<double>(stats.count)) - (mean * mean);
        const double stdev = std::sqrt(variance > kZeroValue ? variance : kZeroValue);
        const double invStd =
            stdev > kEpsilon ? (1.0 / stdev) : kZeroValue;

        meanByFactor[fid] = mean;
        invStdByFactor[fid] = invStd;
    }

    // ── 第二遍: 计算每个标的的 Z-score ──
    std::vector<SymbolFactorScore> result;
    result.reserve(valuesBySymbol.size());

    const std::size_t requiredFactorCount = m_config.factorIds.size();

    // 行业中性化预处理: 若启用，先收集所有 Z-score 再按行业调整
    // 为简化，行业中性化直接在 Z-score 计算后按行业桶去均值
    std::unordered_map<std::string,
        std::unordered_map<std::int32_t, double>> industrySumByFactor;
    std::unordered_map<std::string,
        std::unordered_map<std::int32_t, std::size_t>> industryCountByFactor;

    // 第二遍: 计算原始 Z-score
    for (const auto& [symId, factorVals] : valuesBySymbol) {
        SymbolFactorScore sfs;
        sfs.symbolId = symId;

        for (const auto& [fid, val] : factorVals) {
            const auto meanIt = meanByFactor.find(fid);
            const auto invStdIt = invStdByFactor.find(fid);
            if (meanIt == meanByFactor.end() || invStdIt == invStdByFactor.end())
                continue;

            double zScore = (val - meanIt->second) * invStdIt->second;
            // 夹紧到 [-3, +3]
            zScore = std::clamp(zScore, kMinZScore, kMaxZScore);

            sfs.factorZScores[fid] = zScore;
        }

        // 必须全部因子都有值才纳入
        if (sfs.factorZScores.size() == requiredFactorCount) {
            result.push_back(std::move(sfs));
        }
    }

    // ── 行业中性化（可选）──
    if (m_config.industryNeutral) {
        // 第三遍: 收集每个 (factorId, industryBucket) 的 Z-score 均值
        // 需要从原始 snapshot 获取 industryBucket
        // 构建 symId → industryBucket 映射
        std::unordered_map<std::uint32_t, std::int32_t> industryBySymbol;
        for (const auto& snap : snapshots) {
            if (snap.symbolId != 0 && snap.industryBucket != 0) {
                industryBySymbol[snap.symbolId] = snap.industryBucket;
            }
        }

        // 收集行业统计
        for (auto& sfs : result) {
            const auto indIt = industryBySymbol.find(sfs.symbolId);
            if (indIt == industryBySymbol.end()) continue;
            const std::int32_t bucket = indIt->second;

            for (const auto& [fid, zScore] : sfs.factorZScores) {
                industrySumByFactor[fid][bucket] += zScore;
                ++industryCountByFactor[fid][bucket];
            }
        }

        // 减去行业均值
        for (auto& sfs : result) {
            const auto indIt = industryBySymbol.find(sfs.symbolId);
            if (indIt == industryBySymbol.end()) continue;
            const std::int32_t bucket = indIt->second;

            for (auto& [fid, zScore] : sfs.factorZScores) {
                const auto sumIt = industrySumByFactor.find(fid);
                if (sumIt == industrySumByFactor.end()) continue;
                const auto bucketSumIt = sumIt->second.find(bucket);
                if (bucketSumIt == sumIt->second.end()) continue;

                const auto cntIt = industryCountByFactor.find(fid);
                if (cntIt == industryCountByFactor.end()) continue;
                const auto bucketCntIt = cntIt->second.find(bucket);
                if (bucketCntIt == cntIt->second.end()
                    || bucketCntIt->second == 0) continue;

                zScore -= bucketSumIt->second
                    / static_cast<double>(bucketCntIt->second);
            }
        }
    }

    return result;
}

// ══════════════════════════════════════════════════════════════════════════════
// 阶段2: 加权合成评分
// ══════════════════════════════════════════════════════════════════════════════

std::vector<MultiFactorStrategy::CompositeRecord>
MultiFactorStrategy::computeCompositeScores(
    const std::vector<SymbolFactorScore>& normalized) const
{
    std::vector<CompositeRecord> scores;
    scores.reserve(normalized.size());

    for (const auto& sfs : normalized) {
        double composite = kZeroValue;
        for (const auto& [fid, zScore] : sfs.factorZScores) {
            const auto weightIt = m_config.weights.find(fid);
            if (weightIt != m_config.weights.end()) {
                composite += weightIt->second * zScore;
            }
        }
        scores.push_back({sfs.symbolId, composite});
    }

    return scores;
}

// ══════════════════════════════════════════════════════════════════════════════
// 阶段3: 持仓卖出信号（因子分阈值 + 排名驱动换仓）
// ══════════════════════════════════════════════════════════════════════════════

void MultiFactorStrategy::emitExitSignals(
    const std::vector<CompositeRecord>& allScores,
    const std::unordered_map<std::uint32_t, int>& rankById,
    const RuntimeStrategyContext& context,
    const factor::compute::IMarketDataView* view,
    std::vector<StrategySignal>& outputSignals) const
{
    const auto& currentWeights = context.currentWeights();

    // 诊断：无条件打印前5次，确认是否进入此方法
    static int enterDiag = 0;
    if (++enterDiag <= 5) {
        int hc = 0;
        for (const auto& [sym, w] : currentWeights) if (w > kZeroValue) ++hc;
        INTERNAL_INFO_STREAM << "[MultiFactor] emitExitSignals #" << enterDiag
            << " heldInContext=" << hc
            << " hasView=" << (view != nullptr);
    }

    if (currentWeights.empty()) return;
    if (!view) return;

    // 构建 symbolId → compositeScore 映射
    std::unordered_map<std::uint32_t, double> scoreById;
    scoreById.reserve(allScores.size());
    for (const auto& rec : allScores) {
        if (rec.symbolId != 0) {
            scoreById[rec.symbolId] = rec.compositeScore;
        }
    }

    const int rankExitThreshold = m_config.sellRankMultiplier > kZeroValue
        ? static_cast<int>(m_config.maxPositions * m_config.sellRankMultiplier)
        : 0;  // 0 = 禁用排名卖出

    const auto& viewSymbols = view->symbolStrings();
    const auto& viewInstruments = view->instruments();

    static int exitDiag = 0;
    int scoreExits = 0, rankExits = 0;

    for (const auto& [sym, weight] : currentWeights) {
        if (weight <= kZeroValue) continue;

        // 从 symbol 查找 instrumentId
        std::uint32_t symId = 0;
        for (std::size_t c = 0; c < viewSymbols.size() && c < viewInstruments.size(); ++c) {
            if (viewSymbols[c] == sym) {
                symId = viewInstruments[c].value;
                break;
            }
        }
        if (symId == 0) continue;

        const auto scoreIt = scoreById.find(symId);
        if (scoreIt == scoreById.end()) continue;

        bool shouldSell = false;
        const char* exitReason = "";

        // 条件1: 因子分跌破阈值
        if (scoreIt->second < m_config.sellThreshold) {
            shouldSell = true;
            exitReason = "score";
            ++scoreExits;
        }
        // 条件2: 排名跌出 Top-N * sellRankMultiplier
        else if (rankExitThreshold > 0) {
            const auto rankIt = rankById.find(symId);
            if (rankIt != rankById.end() && rankIt->second > rankExitThreshold) {
                shouldSell = true;
                exitReason = "rank";
                ++rankExits;
            }
        }

        if (shouldSell) {
            outputSignals.push_back(StrategySignal(
                m_instanceId,
                InstrumentId{symId},
                RuntimeOrderSide::Sell,
                scoreIt->second,
                kZeroValue,  // targetWeight = 0 → 平仓
                sym));
        }
    }

    if (++exitDiag <= 5) {
        int heldCount = 0, lookedUp = 0;
        for (const auto& [sym, w] : currentWeights) {
            if (w > kZeroValue) ++heldCount;
        }
        INTERNAL_INFO_STREAM << "[MultiFactor] exits: held=" << heldCount
            << " scoreExits=" << scoreExits
            << " rankExits=" << rankExits
            << " (rankThreshold=" << rankExitThreshold
            << " maxPositions=" << m_config.maxPositions
            << " sellRankMul=" << m_config.sellRankMultiplier << ")";
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// 阶段4: 排名 + Top-N 选股
// ══════════════════════════════════════════════════════════════════════════════

std::vector<MultiFactorStrategy::CompositeRecord>
MultiFactorStrategy::rankAndSelect(
    std::vector<CompositeRecord>& scores) const
{
    // 过滤: compositeScore >= minCompositeScore 且 > 0（只做多）
    std::vector<CompositeRecord> filtered;
    filtered.reserve(scores.size());
    for (auto& rec : scores) {
        if (rec.compositeScore >= m_config.minCompositeScore
            && rec.compositeScore > kZeroValue) {
            filtered.push_back(std::move(rec));
        }
    }

    if (filtered.empty()) return {};

    // nth_element 部分排序（仅当 topN 小于总数时）
    const std::size_t selectionCount =
        std::min(static_cast<std::size_t>(m_config.topN), filtered.size());

    if (selectionCount < filtered.size()) {
        std::nth_element(
            filtered.begin(),
            filtered.begin() + static_cast<std::ptrdiff_t>(selectionCount),
            filtered.end(),
            [](const CompositeRecord& a, const CompositeRecord& b) {
                return a.compositeScore > b.compositeScore;
            });
        filtered.resize(selectionCount);
    }

    // 最终排序: 按 compositeScore 降序
    std::sort(filtered.begin(), filtered.end(),
        [](const CompositeRecord& a, const CompositeRecord& b) {
            return a.compositeScore > b.compositeScore;
        });

    return filtered;
}

// ══════════════════════════════════════════════════════════════════════════════
// 阶段5: 权重分配
// ══════════════════════════════════════════════════════════════════════════════

std::vector<double> MultiFactorStrategy::allocateWeights(
    const std::vector<CompositeRecord>& selected,
    const RuntimeStrategyContext& context,
    const factor::compute::IMarketDataView* view) const
{
    if (selected.empty()) return {};

    const std::size_t n = selected.size();
    std::vector<double> raw(n, kZeroValue);

    switch (m_config.weightScheme) {
    // ── 等权重 ──
    case ::domain::strategies::WeightScheme::EQUAL: {
        const double equalWeight = kFullWeight / static_cast<double>(n);
        std::fill(raw.begin(), raw.end(), equalWeight);
        return raw;
    }

    // ── 信号强度加权 ──
    case ::domain::strategies::WeightScheme::SIGNAL_STRENGTH: {
        double sumAbs = kZeroValue;
        for (const auto& rec : selected) {
            sumAbs += std::abs(rec.compositeScore);
        }
        if (sumAbs <= kEpsilon) {
            const double equalWeight = kFullWeight / static_cast<double>(n);
            std::fill(raw.begin(), raw.end(), equalWeight);
            return raw;
        }
        for (std::size_t i = 0; i < n; ++i) {
            raw[i] = std::abs(selected[i].compositeScore) / sumAbs;
        }
        return raw;
    }

    // ── 市值加权 ──
    case ::domain::strategies::WeightScheme::MARKET_CAP: {
        if (!view) {
            // 回退等权
            const double equalWeight = kFullWeight / static_cast<double>(n);
            std::fill(raw.begin(), raw.end(), equalWeight);
            return raw;
        }
        const auto capField = view->getField(kMarketCapFieldName);
        const auto& instruments = view->instruments();
        const int rows = static_cast<int>(view->dates().size());
        if (!capField.has_value() || capField->data == nullptr
            || instruments.empty() || rows == 0) {
            // 回退等权
            const double equalWeight = kFullWeight / static_cast<double>(n);
            std::fill(raw.begin(), raw.end(), equalWeight);
            return raw;
        }

        const int evalRow = (context.currentEvaluationRow() >= 0
            && context.currentEvaluationRow() < rows)
            ? context.currentEvaluationRow() : (rows - 1);
        const int rowStride = capField->rowStride;

        // 构建 symbolId → col 映射
        std::unordered_map<std::uint32_t, int> colOf;
        colOf.reserve(instruments.size());
        for (std::size_t c = 0; c < instruments.size(); ++c) {
            colOf[instruments[c].value] = static_cast<int>(c);
        }

        double totalCap = kZeroValue;
        std::vector<double> caps(n, kZeroValue);
        for (std::size_t i = 0; i < n; ++i) {
            const auto it = colOf.find(selected[i].symbolId);
            if (it == colOf.end()) continue;
            const int col = it->second;
            double cap = static_cast<double>(
                capField->data[static_cast<std::size_t>(evalRow)
                    * static_cast<std::size_t>(rowStride) + static_cast<std::size_t>(col)]);
            if (std::isfinite(cap) && cap > kZeroValue) {
                caps[i] = cap;
                totalCap += cap;
            }
        }
        if (totalCap <= kEpsilon) {
            const double equalWeight = kFullWeight / static_cast<double>(n);
            std::fill(raw.begin(), raw.end(), equalWeight);
            return raw;
        }
        for (std::size_t i = 0; i < n; ++i) {
            raw[i] = caps[i] / totalCap;
        }
        return raw;
    }

    // ── 风险平价 ──
    case ::domain::strategies::WeightScheme::RISK_PARITY: {
        if (!view) {
            const double equalWeight = kFullWeight / static_cast<double>(n);
            std::fill(raw.begin(), raw.end(), equalWeight);
            return raw;
        }
        const auto closeMat = view->close();
        const auto& instruments = view->instruments();
        const int rows = static_cast<int>(view->dates().size());
        if (closeMat.data == nullptr || instruments.empty() || rows < 2) {
            const double equalWeight = kFullWeight / static_cast<double>(n);
            std::fill(raw.begin(), raw.end(), equalWeight);
            return raw;
        }

        const int evalRow = (context.currentEvaluationRow() >= 0
            && context.currentEvaluationRow() < rows)
            ? context.currentEvaluationRow() : (rows - 1);

        std::unordered_map<std::uint32_t, int> colOf;
        colOf.reserve(instruments.size());
        for (std::size_t c = 0; c < instruments.size(); ++c) {
            colOf[instruments[c].value] = static_cast<int>(c);
        }

        double sumInvVol = kZeroValue;
        std::vector<double> invVols(n, kZeroValue);
        for (std::size_t i = 0; i < n; ++i) {
            const auto it = colOf.find(selected[i].symbolId);
            if (it == colOf.end()) continue;
            double vol = computeDailyVolatility(
                closeMat, evalRow, it->second, kVolatilityLookbackBars);
            if (vol > kZeroValue) {
                invVols[i] = 1.0 / vol;
                sumInvVol += invVols[i];
            }
        }
        if (sumInvVol <= kEpsilon) {
            const double equalWeight = kFullWeight / static_cast<double>(n);
            std::fill(raw.begin(), raw.end(), equalWeight);
            return raw;
        }
        for (std::size_t i = 0; i < n; ++i) {
            raw[i] = invVols[i] / sumInvVol;
        }
        return raw;
    }

    default:
        return {};
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// 阶段6: 买入信号输出
// ══════════════════════════════════════════════════════════════════════════════

void MultiFactorStrategy::emitBuySignals(
    const std::vector<CompositeRecord>& selected,
    const std::vector<double>& weights,
    const std::unordered_map<std::uint32_t, const std::string*>& idToFullSymbol,
    const RuntimeStrategyContext& context,
    std::vector<StrategySignal>& outputSignals) const
{
    if (selected.size() != weights.size()) return;

    const double wMin = std::max(kZeroValue, m_config.minWeightPerStock);
    const double wMax = std::max(wMin, m_config.maxWeightPerStock);

    // 计算当前已持仓数量（跳过已卖出/权重为0的）
    const auto& currentWeights = context.currentWeights();
    int currentHeld = 0;
    std::unordered_set<std::string> heldSymbols;
    for (const auto& [sym, w] : currentWeights) {
        if (w > kZeroValue) {
            ++currentHeld;
            heldSymbols.insert(sym);
        }
    }

    const int maxNewBuys = m_config.maxPositions - currentHeld;
    int newBuys = 0;

    outputSignals.reserve(outputSignals.size() + selected.size());

    for (std::size_t i = 0; i < selected.size(); ++i) {
        const auto symIt = idToFullSymbol.find(selected[i].symbolId);
        if (symIt == idToFullSymbol.end() || !symIt->second) continue;

        const std::string& sym = *symIt->second;
        const bool isHeld = heldSymbols.count(sym) > 0;

        // 已持仓: 发送更新后的目标权重（允许加仓/减仓）
        // 新标的: 仅在未达上限时买入
        if (!isHeld && maxNewBuys <= 0) continue;
        if (!isHeld) ++newBuys;

        const double targetWeight = std::clamp(weights[i], wMin, wMax);

        outputSignals.push_back(StrategySignal(
            m_instanceId,
            InstrumentId{selected[i].symbolId},
            RuntimeOrderSide::Buy,
            selected[i].compositeScore,
            targetWeight,
            sym));

        if (!isHeld && newBuys >= maxNewBuys) break;
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// 辅助方法
// ══════════════════════════════════════════════════════════════════════════════

const factor::compute::IMarketDataView*
MultiFactorStrategy::resolveMarketView(
    const RuntimeStrategyContext& context) noexcept
{
    return static_cast<const factor::compute::IMarketDataView*>(
        context.historicalViewPtr());
}

std::unordered_map<std::uint32_t, const std::string*>
MultiFactorStrategy::buildIdToSymbolMap(
    const factor::compute::IMarketDataView* view)
{
    std::unordered_map<std::uint32_t, const std::string*> result;
    if (!view) return result;

    const auto& instruments = view->instruments();
    const auto& symbols = view->symbolStrings();
    if (instruments.size() != symbols.size()) return result;

    result.reserve(instruments.size());
    for (std::size_t c = 0; c < instruments.size(); ++c) {
        result[instruments[c].value] = &symbols[c];
    }
    return result;
}

double MultiFactorStrategy::computeDailyVolatility(
    const factor::compute::NumericConstMatrixView& closeMat,
    int lastRow, int col, int lookbackBars)
{
    const int rowStride = closeMat.rowStride;
    const int firstRow = std::max(0, lastRow - lookbackBars);
    std::vector<double> returns;
    returns.reserve(static_cast<std::size_t>(lastRow - firstRow));

    for (int r = firstRow + 1; r <= lastRow; ++r) {
        const double prev = static_cast<double>(
            closeMat.data[static_cast<std::size_t>(r - 1)
                * static_cast<std::size_t>(rowStride) + static_cast<std::size_t>(col)]);
        const double cur = static_cast<double>(
            closeMat.data[static_cast<std::size_t>(r)
                * static_cast<std::size_t>(rowStride) + static_cast<std::size_t>(col)]);
        if (prev > kZeroValue && cur > kZeroValue) {
            returns.push_back(cur / prev - kFullWeight);
        }
    }

    if (returns.size() < 2) return kZeroValue;

    double mean = kZeroValue;
    for (double r : returns) mean += r;
    mean /= static_cast<double>(returns.size());

    double var = kZeroValue;
    for (double r : returns) var += (r - mean) * (r - mean);
    var /= static_cast<double>(returns.size() - 1);

    return std::sqrt(var);
}

} // namespace domain::strategy
