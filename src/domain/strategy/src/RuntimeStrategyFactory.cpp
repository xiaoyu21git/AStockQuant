#include "../include/RuntimeStrategyFactory.h"

#include "MultiFactorSelectionStrategy.h"
#include "../../factor/include/factor_compute/IMarketDataView.h"
#include "../../market/include/MarketDataService.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr double kZeroValue = 0.0;
constexpr double kFullWeight = 1.0;
constexpr double kMagnitudeEpsilon = 1e-12;
constexpr const char* kMarketCapFieldName = "market_cap";  // 与 DataFieldKeys::MARKET_CAP 对齐
constexpr int kVolatilityLookbackBars = 20;                // 日波动率回溯窗口(交易日)

struct CandidateSignal final {
    domain::strategy::InstrumentId instrumentId{};
    domain::strategy::RuntimeOrderSide side{domain::strategy::RuntimeOrderSide::Buy};
    double score{0.0};
    double marketCap{0.0};     // 用于市值加权
    double volatility{0.0};    // 用于风险平价(日波动率)
};

[[nodiscard]] double resolveWeightCap(const domain::strategies::MultiFactorSelectionStrategy& strategy,
                                      const domain::strategy::RuntimeStrategyContext& context) noexcept
{
    return (std::max)(kZeroValue, (std::min)(strategy.maxWeightPerStock(), context.maxTargetWeight()));
}

[[nodiscard]] std::vector<CandidateSignal> buildCandidateSignals(
    const domain::strategies::MultiFactorSelectionStrategy& strategy,
    const std::vector<domain::strategies::MultiFactorScore>& scores)
{
    std::vector<CandidateSignal> candidates;
    candidates.reserve(std::min(static_cast<std::size_t>(strategy.maxPositions()), scores.size()));
    for (const auto& score : scores) {
        if (score.symbolId == 0) break;
        const bool useSellSide = strategy.allowsShort() && score.score < kZeroValue;
        if (!useSellSide && !(score.score > kZeroValue)) continue;
        candidates.push_back({domain::strategy::InstrumentId(score.symbolId),
                              useSellSide ? domain::strategy::RuntimeOrderSide::Sell
                                          : domain::strategy::RuntimeOrderSide::Buy,
                              score.score});
    }
    return candidates;
}

/// @brief 判断权重方案是否需要行情参考数据(市值/波动率)
[[nodiscard]] bool needsMarketRefData(const domain::strategies::MultiFactorSelectionStrategy& strategy) noexcept
{
    const auto scheme = strategy.weightScheme();
    return scheme == domain::strategies::WeightScheme::MARKET_CAP
        || scheme == domain::strategies::WeightScheme::RISK_PARITY;
}

/// @brief 当日实时行情参考 (EOD 评估时 PG 尚无今日数据, 用 tick 数据补齐时效)
struct LiveQuoteRef final {
    bool valid{false};
    double scaleRatio{1.0};    // 今收/昨收, 用于缩放 T-1 市值
    double dailyReturn{0.0};   // 今日收益率, 追加进波动率样本
};

/// @brief 从实时行情服务解析当日收盘参考
/// @param fallbackPrevClose preClose 未填充时的分母回退(视图末行 close, 同为不复权口径)
[[nodiscard]] LiveQuoteRef resolveLiveQuote(const std::string& symbol, double fallbackPrevClose)
{
    const auto& live = domain::market::MarketDataService::instance().liveData(symbol);
    if (!live.valid()) return {};
    const double liveClose = live.dailyBar().close();
    const double prevClose = live.preClose() > kZeroValue ? live.preClose() : fallbackPrevClose;
    if (!(liveClose > kZeroValue) || !(prevClose > kZeroValue)) return {};
    const double ratio = liveClose / prevClose;
    return {true, ratio, ratio - kFullWeight};
}

/// @brief 计算单列(标的)近 N 日收益率标准差(日波动率)
/// @param liveQuote 有效时把当日实时收益追加进样本 (与因子层 tick 时效对齐)
[[nodiscard]] double computeDailyVolatility(const factor::compute::NumericConstMatrixView& closeMat,
                                            int lastRow, int col, const LiveQuoteRef& liveQuote)
{
    const int rowStride = closeMat.rowStride;
    const int firstRow = (std::max)(0, lastRow - kVolatilityLookbackBars);
    std::vector<double> returns;
    returns.reserve(static_cast<std::size_t>(lastRow - firstRow) + 1);
    for (int r = firstRow + 1; r <= lastRow; ++r) {
        const double prev = static_cast<double>(closeMat.data[(r - 1) * rowStride + col]);
        const double cur = static_cast<double>(closeMat.data[r * rowStride + col]);
        if (prev > kZeroValue && cur > kZeroValue) returns.push_back(cur / prev - kFullWeight);
    }
    if (liveQuote.valid) returns.push_back(liveQuote.dailyReturn);
    if (returns.size() < 2) return kZeroValue;
    double mean = 0.0;
    for (double r : returns) mean += r;
    mean /= static_cast<double>(returns.size());
    double var = 0.0;
    for (double r : returns) var += (r - mean) * (r - mean);
    var /= static_cast<double>(returns.size() - 1);
    return std::sqrt(var);
}

/// @brief 从行情视图为候选信号填充市值/波动率
///
/// 市值取 market_cap 字段评估行(实盘=最新交易日)值; 波动率由收盘价序列计算。
/// 实盘 EOD 评估时今日数据尚未入库(视图末行 < 当前交易日), 用实时收盘价
/// 缩放 T-1 市值(市值 ∝ 价格, 股本日间不变)并把当日收益追加进波动率样本;
/// 补单/回测场景视图已含目标日数据, 不缩放, 避免双重计算。
/// 视图缺失或标的不在视图中时保持 0, buildRawWeights 会回退等权。
/// @param evaluationRow 评估行号, 负数表示实盘(使用最后一行), 与 NonFactorStrategy 约定一致
void fillMarketRefData(std::vector<CandidateSignal>& candidates,
                       const factor::compute::IMarketDataView* view,
                       int evaluationRow)
{
    if (!view || candidates.empty()) return;
    const auto& instruments = view->instruments();
    const int rows = static_cast<int>(view->dates().size());
    if (instruments.empty() || rows == 0) return;

    // symbolId → 列号 映射
    std::unordered_map<std::uint32_t, int> colOf;
    colOf.reserve(instruments.size());
    for (std::size_t c = 0; c < instruments.size(); ++c)
        colOf.emplace(instruments[c].value, static_cast<int>(c));

    const auto capField = view->getField(kMarketCapFieldName);
    const auto closeMat = view->close();
    const int lastRow = (evaluationRow >= 0 && evaluationRow < rows) ? evaluationRow : (rows - 1);

    // 实盘且视图末行早于当前交易日 → 今日数据缺失, 启用实时价缩放
    const auto& symbolStrings = view->symbolStrings();
    const std::int64_t activeTradingDay =
        domain::market::MarketDataService::instance().activeTradingDay();
    const bool useLiveScaling = evaluationRow < 0
        && activeTradingDay > 0
        && static_cast<std::int64_t>(view->dates().back().value) < activeTradingDay
        && !symbolStrings.empty();

    for (auto& candidate : candidates) {
        const auto it = colOf.find(candidate.instrumentId.value);
        if (it == colOf.end()) continue;
        const int col = it->second;

        LiveQuoteRef liveQuote;
        if (useLiveScaling && col < static_cast<int>(symbolStrings.size())
            && closeMat.data != nullptr) {
            const double lastClose = static_cast<double>(
                closeMat.data[lastRow * closeMat.rowStride + col]);
            liveQuote = resolveLiveQuote(symbolStrings[static_cast<std::size_t>(col)], lastClose);
        }

        if (capField.has_value() && capField->data != nullptr) {
            double cap = static_cast<double>(
                capField->data[lastRow * capField->rowStride + col]);
            if (liveQuote.valid) cap *= liveQuote.scaleRatio;
            if (std::isfinite(cap) && cap > kZeroValue) candidate.marketCap = cap;
        }
        if (closeMat.data != nullptr)
            candidate.volatility = computeDailyVolatility(closeMat, lastRow, col, liveQuote);
    }
}

[[nodiscard]] std::vector<double> buildRawWeights(
    const domain::strategies::MultiFactorSelectionStrategy& strategy,
    const std::vector<CandidateSignal>& candidates)
{
    if (candidates.empty()) return {};

    std::vector<double> raw(candidates.size(), kZeroValue);
    switch (strategy.weightScheme()) {
    case domain::strategies::WeightScheme::EQUAL:
        std::fill(raw.begin(), raw.end(), kFullWeight / static_cast<double>(candidates.size()));
        return raw;
    case domain::strategies::WeightScheme::SIGNAL_STRENGTH: {
        double sum = 0.0;
        for (auto& c : candidates) sum += std::abs(c.score);
        if (sum <= kMagnitudeEpsilon) {
            std::fill(raw.begin(), raw.end(), kFullWeight / static_cast<double>(candidates.size()));
            return raw;
        }
        for (std::size_t i = 0; i < candidates.size(); ++i) raw[i] = std::abs(candidates[i].score) / sum;
        return raw;
    }
    case domain::strategies::WeightScheme::MARKET_CAP: {
        double totalCap = 0.0;
        for (auto& c : candidates) totalCap += std::max(0.0, c.marketCap);
        if (totalCap <= kMagnitudeEpsilon) {
            std::fill(raw.begin(), raw.end(), kFullWeight / static_cast<double>(candidates.size()));
            return raw;
        }
        for (std::size_t i = 0; i < candidates.size(); ++i)
            raw[i] = std::max(0.0, candidates[i].marketCap) / totalCap;
        return raw;
    }
    case domain::strategies::WeightScheme::RISK_PARITY: {
        // 1/波动率 归一化，波动率=0 的标的取均值波动率的倒数
        double avgVol = 0.0; int nz = 0;
        for (auto& c : candidates) if (c.volatility > 0.0) { avgVol += c.volatility; ++nz; }
        if (nz > 0) avgVol /= static_cast<double>(nz);
        double sumInv = 0.0;
        for (auto& c : candidates) {
            double vol = c.volatility > 0.0 ? c.volatility : (avgVol > 0.0 ? avgVol : 0.01);
            sumInv += 1.0 / vol;
        }
        if (sumInv <= kMagnitudeEpsilon) {
            std::fill(raw.begin(), raw.end(), kFullWeight / static_cast<double>(candidates.size()));
            return raw;
        }
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            double vol = candidates[i].volatility > 0.0 ? candidates[i].volatility : (avgVol > 0.0 ? avgVol : 0.01);
            raw[i] = (1.0 / vol) / sumInv;
        }
        return raw;
    }
    default: return {};
    }
}

/// @brief 多因子选择运行时策略
///
/// 因子快照由 StrategyService 通过 IRuntimeFactorService::copySnapshots 注入，
/// 不再持有任何因子适配器。
class MultiFactorSelectionRuntimeStrategy final : public domain::strategy::IRuntimeStrategy {
public:
    MultiFactorSelectionRuntimeStrategy(
        std::shared_ptr<const domain::strategies::MultiFactorSelectionStrategy> strategyDefinition,
        domain::strategy::StrategyInstanceId strategyInstanceId)
        : strategyDefinition_(std::move(strategyDefinition))
        , strategyInstanceId_(strategyInstanceId)
    {
    }

    [[nodiscard]] domain::strategy::StrategyInstanceId instanceId() const noexcept override { return strategyInstanceId_; }
    [[nodiscard]] bool isEnabled() const noexcept override { return strategyDefinition_ && strategyDefinition_->isEnabled(); }
    [[nodiscard]] domain::strategy::rules::RuleSetId ruleSetId() const noexcept override {
        return domain::strategy::rules::kRuleSetAllPass;
    }
    [[nodiscard]] bool usesFactors() const noexcept override { return true; }

    void evaluate(const std::vector<domain::strategy::RuntimeFactorSnapshot>& factorSnapshots,
                  const domain::strategy::RuntimeStrategyContext& context,
                  std::vector<domain::strategy::StrategySignal>& outputSignals) override
    {
        if (!strategyDefinition_ || !strategyDefinition_->isConfigured() || !context.isValid()
            || context.strategyInstanceId() != strategyInstanceId_ || factorSnapshots.empty()) return;

        const double wCap = resolveWeightCap(*strategyDefinition_, context);
        const double wMin = strategyDefinition_->minWeightPerStock();
        if (!(wCap > kZeroValue) || wCap < wMin) return;

        auto scores = strategyDefinition_->computeCompositeScores(factorSnapshots);
        auto candidates = buildCandidateSignals(*strategyDefinition_, scores);
        // 市值加权/风险平价需要行情参考数据(市值/波动率), 从上下文行情视图填充
        if (needsMarketRefData(*strategyDefinition_)) {
            fillMarketRefData(candidates,
                              static_cast<const factor::compute::IMarketDataView*>(
                                  context.historicalViewPtr()),
                              context.currentEvaluationRow());
        }
        auto rawWeights = buildRawWeights(*strategyDefinition_, candidates);
        if (candidates.empty() || rawWeights.size() != candidates.size()) return;

        outputSignals.reserve(outputSignals.size() + candidates.size());
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const double bw = std::max(wMin, std::min(rawWeights[i], wCap));
            // 策略只输出纯信号: (symbol, side, targetWeight, score)
            // 意图由调度层 buildPositionAwareOrders 根据持仓对比确定
            auto sig = domain::strategy::StrategySignal(
                strategyInstanceId_, candidates[i].instrumentId,
                candidates[i].side, candidates[i].score, bw);
            outputSignals.push_back(std::move(sig));
        }
    }

private:
    std::shared_ptr<const domain::strategies::MultiFactorSelectionStrategy> strategyDefinition_;
    domain::strategy::StrategyInstanceId strategyInstanceId_{0};
};

} // anonymous namespace

namespace domain::strategy {

std::shared_ptr<IRuntimeStrategy> createMultiFactorSelectionRuntimeStrategy(
    std::shared_ptr<const ::domain::strategies::MultiFactorSelectionStrategy> strategyDefinition,
    StrategyInstanceId strategyInstanceId)
{
    if (!strategyDefinition || strategyInstanceId == 0) return {};
    return std::make_shared<MultiFactorSelectionRuntimeStrategy>(
        std::move(strategyDefinition), strategyInstanceId);
}

std::unique_ptr<StrategyEngine> createMultiFactorRuntimeEngine(MultiFactorRuntimeEngineSetup setup)
{
    if (!setup.strategyDefinition || setup.strategyInstanceId == 0
        || !setup.context.isValid() || setup.context.strategyInstanceId() != setup.strategyInstanceId)
        return nullptr;

    auto engine = StrategyEngine::builder()
        .build();
    const auto result = engine->registerStrategy(
        createMultiFactorSelectionRuntimeStrategy(setup.strategyDefinition, setup.strategyInstanceId),
        setup.context);
    if (!result.isOk()) return nullptr;
    return engine;
}

} // namespace domain::strategy
