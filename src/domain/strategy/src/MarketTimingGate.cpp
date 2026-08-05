#include "MarketTimingGate.h"

namespace domain::strategy {

MarketTimingGate::MarketTimingGate() = default;

TimingResult MarketTimingGate::evaluate(const MarketTimingSnapshot& s) const {
    if (m_useModel && m_model) return evaluateModel(s);
    return evaluateRules(s);
}

TimingResult MarketTimingGate::evaluateRules(const MarketTimingSnapshot& s) const {
    TimingResult r;

    if (!s.isValid()) {
        r.targetExposure = 0.5;
        r.reason = "市场快照数据缺失, 默认中性仓位";
        return r;
    }

    const bool aboveMA60  = s.indexClose > s.ma60;
    const bool aboveMA20  = s.indexClose > s.ma20;
    const bool trendUp    = s.ma20AboveMa60 && s.ma20Rising;
    const bool breadthStrong = s.advanceRatio > 0.50;
    const bool breadthWeak   = s.advanceRatio < 0.35;
    const bool breadthCrash  = s.advanceRatio < 0.30;
    const bool highVol       = s.atrPercent > 0.03 || s.crossSectionalVol > 0.03;

    // 状态 1: 进攻 — 均线多头排列 + 宽度强势
    if (aboveMA60 && trendUp && breadthStrong) {
        r.targetExposure = 1.0;
        r.allowNewEntries = true;
        r.forceLiquidate = false;
        r.reason = "进攻: 多头排列 + 宽度强势";
    }
    // 状态 2: 谨慎 — 站上 MA60 但宽度不足
    else if (aboveMA60 && breadthWeak) {
        r.targetExposure = 0.5;
        r.allowNewEntries = true;
        r.forceLiquidate = false;
        r.reason = "谨慎: 站上MA60但宽度不足";
    }
    // 状态 3: 防御 — 跌破 MA60 但还在 MA20 上方
    else if (!aboveMA60 && aboveMA20) {
        r.targetExposure = 0.2;
        r.allowNewEntries = false;
        r.forceLiquidate = false;
        r.reason = "防御: 跌破MA60, 仅保留底仓";
    }
    // 状态 4: 空仓 — 跌破 MA20 + 趋势向下 + 宽度崩溃
    else if (!aboveMA20 && !trendUp && breadthCrash) {
        r.targetExposure = 0.0;
        r.allowNewEntries = false;
        r.forceLiquidate = true;
        r.reason = "空仓: 跌破MA20 + 趋势向下 + 宽度崩溃";
    }
    // 状态 5: 高波动防御 — 不管均线如何, 波动率过高就降仓
    else if (highVol) {
        r.targetExposure = 0.3;
        r.allowNewEntries = false;
        r.forceLiquidate = false;
        r.reason = "高波动防御: ATR或截面波动率过高";
    }
    // 默认
    else {
        r.targetExposure = 0.5;
        r.allowNewEntries = true;
        r.forceLiquidate = false;
        r.reason = "中性: 无明确信号";
    }

    return r;
}

TimingResult MarketTimingGate::evaluateModel(const MarketTimingSnapshot& s) const {
    TimingResult r;
    if (!m_model) {
        r.targetExposure = 0.5;
        r.reason = "模型未加载";
        return r;
    }

    double prob = m_model->predictUpProbability(s);
    if (prob < 0.0) {
        r.targetExposure = 0.5;
        r.reason = "模型推理失败";
        return r;
    }

    // 概率 → 仓位映射
    if (prob > 0.65)       { r.targetExposure = 1.0; r.reason = "模型看涨 (高置信度)"; }
    else if (prob > 0.55)  { r.targetExposure = 0.7; r.reason = "模型看涨"; }
    else if (prob > 0.45)  { r.targetExposure = 0.5; r.reason = "模型中性"; }
    else if (prob > 0.35)  { r.targetExposure = 0.3; r.reason = "模型看跌"; }
    else                   { r.targetExposure = 0.0; r.forceLiquidate = true; r.reason = "模型看跌 (高置信度)"; }

    r.allowNewEntries = (r.targetExposure > 0.2);
    return r;
}

} // namespace domain::strategy
