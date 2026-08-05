#pragma once
// MarketTimingGate — 大盘择时闸门
// 每日盘后根据指数均线结构 + 市场宽度 + 波动率决定目标仓位

#include <memory>
#include <string>

namespace domain::strategy {

// ── 择时输出 ──
struct TimingResult {
    bool allowNewEntries = true;   // false = 当日只平仓不开仓
    bool forceLiquidate = false;   // true  = 当日强制清仓
    double targetExposure = 1.0;   // 目标仓位比例 [0.0, 1.0]
    std::string reason;            // 诊断信息 (日志用)
};

// ── 市场快照 (由 StrategyEngine 每日回测开始时填充) ──
struct MarketTimingSnapshot {
    // 大盘指数 (沪深300)
    double indexClose = 0.0;
    double ma20 = 0.0;
    double ma60 = 0.0;
    bool   ma20AboveMa60 = false;
    bool   ma20Rising = false;      // MA20 斜率 > 0

    // 市场宽度
    double advanceRatio = 0.0;      // 上涨家数 / 总家数
    double newHighRatio = 0.0;      // 创20日新高占比
    int    limitUpCount = 0;        // 涨停家数

    // 波动
    double atrPercent = 0.0;        // ATR / close, 百分比
    double crossSectionalVol = 0.0; // 截面收益率标准差

    // 成交额
    double volumeRatio = 0.0;       // 当日成交额 / 20日均成交额

    bool isValid() const { return indexClose > 1e-9 && ma20 > 1e-9 && ma60 > 1e-9; }
};

// ── 择时模型接口 (v0.14 AI 升级用) ──
class ITimingModel {
public:
    virtual ~ITimingModel() = default;
    /// @return 大盘上涨概率 [0, 1], <0 = 模型未加载
    virtual double predictUpProbability(const MarketTimingSnapshot& snapshot) = 0;
};

// ── 择时闸门 ──
class MarketTimingGate {
public:
    MarketTimingGate();

    /// @brief 核心接口: 输入快照 → 输出择时决策
    TimingResult evaluate(const MarketTimingSnapshot& snapshot) const;

    /// 切换模式: 规则 (v0.13) vs 模型 (v0.14)
    void useRules()   { m_useModel = false; }
    void useModel(std::shared_ptr<ITimingModel> model) {
        m_model = std::move(model); m_useModel = true;
    }

private:
    TimingResult evaluateRules(const MarketTimingSnapshot& s) const;
    TimingResult evaluateModel(const MarketTimingSnapshot& s) const;

    bool m_useModel = false;
    std::shared_ptr<ITimingModel> m_model;
};

} // namespace domain::strategy
