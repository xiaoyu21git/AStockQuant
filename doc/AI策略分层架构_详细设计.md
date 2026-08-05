# AI 策略分层架构 — 详细设计文档 v2.1

> 核心变化 (v2.0→v2.1): 规则从"闸门过滤"升级为"信号评分工"
> 因子和规则共同决定买卖信号，不再一层拦一层

---

## 一、问题诊断 (v2.0 架构缺陷)

v2.0 架构的三层各自独立，互不通信:

```
因子 → 候选池 (Top-200, 只管"哪只好")
策略 → MA金叉打分 (不看因子值, 技术指标)
规则 → 过/不过 (二值闸门, 不"加分")
```

**根因**: 策略的 `evaluateSymbol()` 只读 `closePrices`, 完全不知道因子值。规则只做二值判断 (Pass/Block), 形态命中不增加信号强度。

**结果**: 用户看到"全是规则在跑, 策略本身没发出信号"——策略的输出就是MA金叉, 跟AI因子无关。

---

## 二、v2.1 信号模型

### 核心改动

```
旧: 因子→候选池 → 策略(MA打分) → 规则(过/不过) → 信号
新: 因子→候选池 → 综合评分工(因子分+规则分) → 信号
                        ↑                    ↑
                   AI因子+传统因子       所有形态规则命中数
```

规则不再是"拦路的闸门", 而是"加分的评委"。

### 评分公式

```
factorScore  = compositeScore(symbol)              // 0~1, 因子加权排名分
ruleScore    = ruleHitCount / ruleCount            // 0~1, 形态命中占比
entryScore   = w_factor × factorScore + w_rule × ruleScore

if entryScore > entryThreshold  → Buy  (权重基于得分)
if entryScore < exitThreshold   → Sell
```

| 参数 | 默认值 | 含义 |
|------|--------|------|
| `w_factor` | 0.5 | 因子权重 |
| `w_rule` | 0.5 | 规则形态权重 |
| `entryThreshold` | 0.6 | 买入最低分 |
| `exitThreshold` | 0.3 | 持仓跌破此分则卖出 |

### 规则评分的具体逻辑

每只候选标的, 遍历所有入口类规则模板:

```
buyHitCount = 0
buyRuleCount = 0

for each template in enabledEntryTemplates:
    for each rule in template:
        result = rule.evaluate(candidateCtx, marketSnapshot)
        if result == Pass:
            buyHitCount += 1
        buyRuleCount += 1
    // 同一模板内多条规则: 全部命中才算该模板命中 (加重权重)
    // 或: 每条规则独立计数 (简单方案, 先采用)

ruleScore = min(buyHitCount / max(buyRuleCount, 1), 1.0)
```

出场用同一逻辑:

```
for each template in enabledExitTemplates:
    if positionTemplateHits > threshold:
        exitScoreBoost += 1

if entryScore < exitThreshold OR exitScoreBoost > 0:
    → Sell
```

---

## 三、规则保留的硬闸门

以下规则仍保持**二进制否决权**, 不参与评分:

| 规则类型 | 行为 | 原因 |
|----------|------|------|
| ST/\*ST | 直接剔除候选池 | 法规风险 |
| 当日涨跌停 | 无法买入/卖出 | 物理限制 |
| 停牌 | 跳过 | 无法交易 |
| 日成交额 < 2000万 | 剔除 | 流动性 |
| 一字板 | 剔除 | 无法成交 |
| 基本面暴雷 (净利润<0) | 剔除 | 信用风险 |

这些规则在**评分工之前**运行, 命中直接移除候选资格, 不进入评分。

---

## 四、策略实体: CompositeScoringStrategy

替代现有的 `TrendFollowingStrategy` 等纯技术指标策略:

```cpp
class CompositeScoringStrategy : public IRuntimeStrategy {
public:
    void evaluate(const std::vector<RuntimeFactorSnapshot>& snapshots,
                  const RuntimeStrategyContext& ctx,
                  std::vector<StrategySignal>& out) override
    {
        // 1. 从因子处理器取候选池 + 综合因子分
        auto symbols = m_processor->allSymbols();

        // 2. 对每个候选标的: 计算规则分 → 综合分 → 信号
        for (const auto& sym : symbols) {
            double factorScore = m_processor->compositeScore(sym);

            // 规则评分: 所有入口形态规则对当前标的打分
            double ruleScore = computeRuleScore(sym, RulePhase::Entry, ctx);

            double finalScore = m_cfg.wFactor * factorScore + m_cfg.wRule * ruleScore;

            if (finalScore >= m_cfg.entryThreshold) {
                double w = scoreToWeight(finalScore);  // 分越高仓位越重
                out.push_back(BuySignal(sym, finalScore, w));
            }
        }

        // 3. 持仓检查: 规则出场评分
        for (auto& pos : ctx.positions()) {
            double exitScore = computeRuleScore(pos.symbol, RulePhase::Exit, ctx);
            if (m_processor->compositeScore(pos.symbol) < m_cfg.exitThreshold
                || exitScore > 0.3) {
                out.push_back(SellSignal(pos.symbol, exitScore));
            }
        }
    }

private:
    double computeRuleScore(const std::string& symbol,
                            RulePhase phase,
                            const RuntimeStrategyContext& ctx) {
        int hits = 0, total = 0;
        for (const auto& tpl : m_entryTemplates) {
            for (const auto& rule : tpl.rules) {
                ++total;
                if (rule.evaluate(symbol, ctx) == RuleResult::Pass) ++hits;
            }
        }
        return total > 0 ? static_cast<double>(hits) / total : 0.0;
    }
};
```

---

## 五、实施计划

### Step 1: RuleGate 增加评分接口

在现有 `RuleGate` 上新增方法, 不影响现有功能:

```cpp
// 新增: 计算规则形态命中得分 (0~1)
double RuleGate::entryScore(const RuleCandidateContext& ctx,
                            const RuleMarketSnapshot& snapshot) const;

// 新增: 计算持仓出场的规则得分 (0~1)
double RuleGate::exitScore(const RuleCandidateContext& ctx,
                           const RuleMarketSnapshot& snapshot) const;
```

现有 `allowSignal()` 和 `positionAction()` 保持不变, 策略可按需调用评分或闸门。

### Step 2: CompositeScoringStrategy 实现

新增策略类型, 使用 `entryScore()` + `factorScore` 综合打分。

### Step 3: 回归验证

| 测试 | 对照 |
|------|------|
| v9 因子满仓 | 基线 IC=0.27, 收益为负 |
| v2.0 分层 | 规则择时 + 因子选股 |
| **v2.1 评分工** | **因子分 + 规则分 综合打分** |

验收: 年化收益转正, G1 显著跑赢 G5, 买卖信号日志中能看到因子+规则各贡献的比例。

---

> v2.1 核心变化: 规则从闸门→评委, 因子从选池→共同打分
> 当前状态: 设计定稿, 待编码实施
