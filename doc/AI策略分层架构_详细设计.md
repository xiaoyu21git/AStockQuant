# AI 策略分层架构 — 详细设计文档 v1.0

> 基于需求文档 v2.0 | 基于现有策略系统代码审计 | 2026-07-31

---

## 一、现有架构复用分析

策略系统 (`StrategyEngine`) 已经提供了三层管线：

```
StrategyEngine::backtest()
  │
  ├─ Phase 1: 因子候选池
  │   FactorSignalProcessor::updateSnapshot(factorValues)
  │   ICandidatePoolSelector::selectPool() → Top-N 候选池
  │
  ├─ Phase 2: 策略评估
  │   IRuntimeStrategy::evaluate() → 候选池内单股打分
  │   RulePipeline::filterBuySignals() → 规则过滤
  │   OrderGenerator::generate() → 目标仓位
  │
  └─ Phase 3: 风控扫描
      RiskEvaluator::evaluate() → 止损/止盈检查
```

| v0.13 分层 | 复用现有组件 | 新增 |
|------------|------------|------|
| 选股层 | `FactorSignalProcessor` + `RankOnlyPoolSelector` | 无需新增 |
| 过滤层 | `RuleGate(filterTemplates)` | ST/流动性规则模板 |
| **择时层** | — | **`MarketTimingGate`** |
| 配仓层 | `OrderGenerator::generate()` | 无需新增 |
| **风控层** | `RiskEvaluator`(部分) | **回撤熔断 + 单日止损扩展** |

**核心工作量：两个新类 + 规则模板，不破坏现有管线。**

---

## 二、新增类设计

### 2.1 MarketTimingGate — 择时闸门

**职责**：判断当前市场是否允许开仓，输出目标仓位比例。

**位置**：`src/domain/strategy/include/MarketTimingGate.h`

```cpp
namespace domain::strategy {

struct TimingResult {
    bool allowNewEntries = true;   // false = 当日禁止开新仓
    bool forceLiquidate = false;   // true = 当日强制清仓
    double targetExposure = 1.0;  // 目标仓位比例 [0.0, 1.0]
    std::string reason;            // 诊断信息
};

class MarketTimingGate {
public:
    /// @brief 输入当日大盘快照 → 输出择时决策
    /// @param snapshot 市场截面数据 (指数价格, 均线, 宽度, 波动率)
    TimingResult evaluate(const MarketTimingSnapshot& snapshot) const;

    /// 配置择时规则 (v1: 固定规则, v2: 模型权重)
    void setRules(const TimingRuleConfig& config);
    void setModel(std::shared_ptr<ITimingModel> model);  // v0.14 AI升级

private:
    TimingRuleConfig m_rules;
    std::shared_ptr<ITimingModel> m_model;  // v0.14
};

// ── 市场快照 (每日回测开始时由 StrategyEngine 填充) ──
struct MarketTimingSnapshot {
    // 大盘指数
    double indexClose = 0.0;
    double ma20 = 0.0;
    double ma60 = 0.0;
    double ma20Slope = 0.0;   // MA20 斜率 (上升/下降趋势)

    // 市场宽度
    double advanceRatio = 0.0;   // 上涨家数占比
    double newHighRatio = 0.0;   // 创 N 日新高占比
    double limitUpCount = 0;     // 涨停家数

    // 波动
    double atrPercent = 0.0;     // ATR / close
    double vixProxy = 0.0;       // 截面波动率 (≈ market_volatility from v9)

    // 成交额
    double volumeRatio = 0.0;    // 当日成交额 / 20日均成交额

    std::string date;
};

} // namespace domain::strategy
```

**v1 规则逻辑**（硬编码在 `evaluate()` 内）：

```
若 indexClose > ma60 且 ma20 > ma60 且 advanceRatio > 0.5:
    → targetExposure = 1.0  (满仓进攻)

若 indexClose > ma60 且 advanceRatio < 0.35:
    → targetExposure = 0.5  (半仓谨慎)

若 indexClose < ma60 且 indexClose >= ma20:
    → targetExposure = 0.2  (防御仓位)

若 indexClose < ma20 且 ma20Slope < 0 且 advanceRatio < 0.3:
    → targetExposure = 0.0, forceLiquidate = true  (空仓)

默认:
    → targetExposure = 0.5 (中性)
```

### 2.2 TimedCircuitBreaker — 风控扩展

**职责**：回撤熔断 + 单日亏损限制（独立于 `RiskEvaluator` 的止损止盈）。

**位置**：`src/domain/strategy/include/TimedCircuitBreaker.h`

```cpp
namespace domain::strategy {

struct CircuitBreakerState {
    bool tradingHalted = false;      // 禁止交易
    int haltDaysRemaining = 0;       // 剩余熔断交易日
    double peakEquity = 0.0;         // 历史最高净值
    double dailyStartEquity = 0.0;   // 当日开盘净值
};

class TimedCircuitBreaker {
public:
    /// @brief 每日开盘前检查
    bool isHalted() const { return m_state.tradingHalted; }

    /// @brief 每日收盘后更新状态 (传入当日净值)
    void updateEndOfDay(double endOfDayEquity);

    /// @brief 日内实时检查 (传入当前净值)
    /// @return true = 触发熔断, 需立即减仓
    bool checkIntraday(double currentEquity);

    void reset();

private:
    CircuitBreakerState m_state;
    // 参数
    double m_maxDrawdown = 0.15;      // 回撤 15% 熔断
    int    m_haltDays = 5;            // 熔断天数
    double m_dailyLossLimit = 0.03;   // 单日亏损 3% 减仓
    double m_reduceTo = 0.5;          // 减仓至 50%
};

} // namespace domain::strategy
```

---

## 三、集成方案

### 3.1 StrategyEngine::backtest() 改动

在现有管线中插入择时和风控逻辑（改动量约 30 行）：

```cpp
// StrategyEngine::backtest() 伪代码
for (each trading date) {
    // 1. 更新候选池 (现有)
    factorSignalProcessor.updateSnapshot(factorValues);
    auto pool = poolSelector->selectPool(factorSignalProcessor);

    // 2. 择时判断 (新增) ← MarketTimingGate
    MarketTimingSnapshot timingSnapshot = buildTimingSnapshot(view, date);
    TimingResult timing = m_timingGate.evaluate(timingSnapshot);

    // 3. 风控检查 (新增) ← TimedCircuitBreaker
    if (m_circuitBreaker.isHalted()) {
        m_circuitBreaker.updateEndOfDay(todayEquity);
        continue;  // 跳过今天
    }
    if (timing.forceLiquidate) {
        generateLiquidationOrders();  // 全清
        continue;
    }

    // 4. 策略评估 (现有)
    auto orders = stepBatch(marketDataBatch);

    // 5. 择时仓位缩放 (新增)
    if (!timing.allowNewEntries) {
        orders = filterOutBuyOrders(orders);  // 只保留卖出
    }
    orders = scalePositionSizes(orders, timing.targetExposure);

    // 6. 规则闸门 (现有)
    orders = rulePipeline.filterBuySignals(orders, ...);

    // 7. 执行 + 更新净值 (现有)
    executeOrders(orders);
    m_circuitBreaker.updateEndOfDay(todayEquity);
    if (m_circuitBreaker.checkIntraday(todayEquity)) {
        emergencyReduce();
    }
}
```

### 3.2 MarketTimingSnapshot 构建

`buildTimingSnapshot(view, date)` 复用现有 `BacktestRuleVariableProvider` 里的市场统计量——它已经计算了 97 个市场变量（`RuleMarketSnapshot`），包括指数收盘价、MA、涨跌比、宽度、新高新低等。择时快照直接从它取值。

### 3.3 需要改动的文件清单

| 文件 | 改动类型 | 说明 |
|------|----------|------|
| `MarketTimingGate.h/.cpp` | **新增** | 择时闸门类 |
| `TimedCircuitBreaker.h/.cpp` | **新增** | 风控熔断类 |
| `StrategyEngineFacade.cpp` | 修改 | 插入择时/风控逻辑 |
| `IStrategyService.h` | 修改 | Builder 增 `setTimingConfig` / `setBreakerConfig` |
| CMakeLists | 修改 | 添加新文件 |
| 规则模板 JSON | **新增** | ST/涨跌停/流动性过滤规则 |

---

## 四、数据流 (盘后时序)

```
15:30 盘后
  │
  ├─ 1. AI因子计算   C++ FeatureTensorBuilder + ONNX → factorValues
  ├─ 2. 传统因子计算 已有 C++ FactorEngine → factorValues
  ├─ 3. 选股层       FactorSignalProcessor.compositeScore() → 排序
  │                  RankOnlyPoolSelector → Top-200 候选池
  ├─ 4. 过滤层       RuleGate.filterBuySignals()
  │                  ST/停牌/涨跌停/流动性 规则模板
  │                  → ~150 候选标的
  ├─ 5. 择时层       MarketTimingGate.evaluate(snapshot)
  │                  大盘MA/宽度/波动 → targetExposure
  ├─ 6. 配仓层       OrderGenerator.generate()
  │                  因子值加权 + singleStockCap(5%)
  ├─ 7. 风控层       TimedCircuitBreaker
  │                  回撤熔断 / 单日减仓
  │
  └─ → 调仓指令 → 次日开盘执行
```

---

## 五、测试验证计划

### 5.1 单元测试

- `MarketTimingGate`: 手工构造 snapshot，验证 4 种市场状态下的输出
- `TimedCircuitBreaker`: 模拟净值序列，验证 15% 回撤熔断 + 5 日恢复

### 5.2 回测验证

| 测试场景 | 预期 |
|----------|------|
| 2020-2026 全区间 | 年化转正, maxDD < 25% |
| 2022年熊市 | 择时空仓 > 80% 交易日 |
| 2024年9.24牛市 | 满仓, G1 跑赢 G5 |
| v9 因子满仓 vs 分层策略 | 策略 Sharpe > 因子满仓 Sharpe |

---

## 六、实施顺序

| 步骤 | 预估时间 |
|------|----------|
| 1. `MarketTimingGate` 实现 + 单元测试 | 1 天 |
| 2. `TimedCircuitBreaker` 实现 | 0.5 天 |
| 3. `StrategyEngine::backtest()` 集成 | 1 天 |
| 4. 规则模板 (ST/流动性) | 0.5 天 |
| 5. 端到端回测 + 参数调优 | 1 天 |
| **合计** | **4 天** |

---

> 设计原则: 最小侵入、复用优先、分层可替换、全规则可回测
