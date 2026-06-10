#include "../include/RiskControl.h"
#include <cstdint>
#include <string>
#include <vector>

namespace domain::strategy {

// ── LimitUpDownRule ──
LimitUpDownRule::LimitUpDownRule(double lu, double ld, int tc, int ic)
    : lu_(lu), ld_(ld), tc_(tc), ic_(ic) {}

RiskEvaluationResult LimitUpDownRule::evaluate(int ti, int ii,
    const factor::compute::SignalSet&,
    const factor::compute::IMarketDataView& view) const {
    RiskEvaluationResult r;
    if (ti < 0 || ti >= tc_ || ii < 0 || ii >= ic_) { r.tradable = false; r.blockReason = "index"; return r; }
    auto o = view.open(), c = view.close();
    if (o.rowCount != tc_ || o.columnCount != ic_ || c.rowCount != tc_ || c.columnCount != ic_) return r;
    size_t oi = (size_t)ti * o.rowStride + ii, ci = (size_t)ti * c.rowStride + ii;
    float prev = (ti > 0) ? c.data[ci - c.rowStride] : o.data[oi];
    float cur = c.data[ci];
    float ret = (prev > 1e-8f) ? (cur - prev) / prev : 0.0f;
    if (ret >= lu_ || ret <= ld_) { r.tradable = false; r.blockReason = "涨跌停"; }
    return r;
}

// ── LiquidityRule ──
LiquidityRule::LiquidityRule(double mv, int tc, int ic) : mv_(mv), tc_(tc), ic_(ic) {}

RiskEvaluationResult LiquidityRule::evaluate(int ti, int ii,
    const factor::compute::SignalSet&,
    const factor::compute::IMarketDataView& view) const {
    RiskEvaluationResult r;
    if (ti < 0 || ti >= tc_ || ii < 0 || ii >= ic_) { r.tradable = false; r.blockReason = "index"; return r; }
    auto v = view.volume();
    if (v.rowCount != tc_ || v.columnCount != ic_) return r;
    if (v.data[(size_t)ti * v.rowStride + ii] < mv_) { r.tradable = false; r.blockReason = "流动性不足"; }
    return r;
}

// ── SuspensionRule ──
SuspensionRule::SuspensionRule(std::vector<uint8_t> m, int tc, int ic)
    : mask_(std::move(m)), tc_(tc), ic_(ic) {}

RiskEvaluationResult SuspensionRule::evaluate(int ti, int ii, const factor::compute::SignalSet&, const factor::compute::IMarketDataView&) const {
    RiskEvaluationResult r;
    if (ti < 0 || ti >= tc_ || ii < 0 || ii >= ic_) { r.tradable = false; r.blockReason = "index"; return r; }
    if (mask_[(size_t)ti * ic_ + ii]) { r.tradable = false; r.blockReason = "停牌"; }
    return r;
}

// ── SecurityListRule ──
SecurityListRule::SecurityListRule(std::vector<int> b) : blocked_(std::move(b)) {}

RiskEvaluationResult SecurityListRule::evaluate(int, int ii, const factor::compute::SignalSet&, const factor::compute::IMarketDataView&) const {
    RiskEvaluationResult r;
    for (int idx : blocked_) if (idx == ii) { r.tradable = false; r.blockReason = "黑名单"; break; }
    return r;
}

// ── PositionLimitRule ──
PositionLimitRule::PositionLimitRule(double mq, int tc, int ic) : maxQty_(mq), tc_(tc), ic_(ic) {}

RiskEvaluationResult PositionLimitRule::evaluate(int, int, const factor::compute::SignalSet&, const factor::compute::IMarketDataView&) const {
    return {}; // 需要持仓数据
}

// ── MaxDrawdownRule ──
MaxDrawdownRule::MaxDrawdownRule(double dd, double pk, int tc, int ic) : maxDD_(dd), peak_(pk), tc_(tc), ic_(ic) {}

RiskEvaluationResult MaxDrawdownRule::evaluate(int, int, const factor::compute::SignalSet&, const factor::compute::IMarketDataView&) const {
    return {}; // 需要净值序列
}

// ── LeverageLimitRule ──
LeverageLimitRule::LeverageLimitRule(double ml, double eq, int tc, int ic) : maxLev_(ml), eq_(eq), tc_(tc), ic_(ic) {}

RiskEvaluationResult LeverageLimitRule::evaluate(int, int, const factor::compute::SignalSet&, const factor::compute::IMarketDataView&) const {
    return {}; // 需要保证金数据
}

// ── AccountManager ──
AccountManager::AccountManager(double initCash) : cash_(initCash), equity_(initCash) {}
AccountInfo AccountManager::snapshot() const { return {cash_, equity_, 0.0, 0.0}; }
void AccountManager::processTrade(double value) { cash_ += value; equity_ += value; }
void AccountManager::reset(double initCash) { cash_ = initCash; equity_ = initCash; }

// ── RiskPipeline ──
void RiskPipeline::addRule(std::unique_ptr<IRiskRule> r) { rules_.push_back(std::move(r)); }
void RiskPipeline::clear() { rules_.clear(); }

std::vector<uint8_t> RiskPipeline::evaluate(
    const factor::compute::SignalSet& signal,
    const factor::compute::IMarketDataView& view) const {
    int T = (int)signal.dates.size(), N = (int)signal.instruments.size();
    std::vector<uint8_t> mask((size_t)T * N, 0);
    for (int t = 0; t < T; ++t)
        for (int n = 0; n < N; ++n)
            for (auto& rule : rules_)
                if (!rule->evaluate(t, n, signal, view).tradable) { mask[(size_t)t * N + n] = 1; break; }
    return mask;
}

} // namespace domain::strategy