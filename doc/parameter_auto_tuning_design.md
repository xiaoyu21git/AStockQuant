# 策略参数自动调优 — 架构设计文档

本文定义 v0.16.0 参数自动调优模块的完整架构，包括领域层算法、桥接层调度、参数空间定义和搜索策略。

## 1. 目标与范围

### 1.1 目标

为回测引擎提供系统化的参数搜索能力，替代手工试参：

- **网格搜索**: 穷举参数组合，适合粗粒度探索（3-5 参数，每维 3-7 个候选值）
- **贝叶斯优化**: 基于高斯过程的序贯优化，适合高维细粒度搜索（5+ 参数，连续空间）

### 1.2 范围

| 维度 | 范围 |
|------|------|
| 策略类型 | 全部 11 种（`StrategyType` 枚举），按 `behaviorKind` 路由参数空间 |
| 参数类型 | `int`, `double`, `bool`, `enum`, `vector<double>`（因子权重） |
| 搜索算法 | V1: 网格搜索 · V2: 贝叶斯优化（GP-UCB / EI） |
| 目标函数 | 单目标（Sharpe / 年化收益 / Calmar / 自定义打分） |
| 约束 | 最大回撤上限、最低交易次数、最低胜率 |

### 1.3 非范围

- **不涉及**因子计算逻辑修改
- **不涉及**规则模板参数优化（`ParamOverrides` 走独立路径）
- **不涉及**实时在线调参（仅离线批量回测）

---

## 2. 架构总览

```
┌─────────────────────────────────────────────────────────┐
│                      QML UI Layer                        │
│  TuningConfigPanel  ──→  ParameterTuningBridge          │
├─────────────────────────────────────────────────────────┤
│                    Bridge Layer                          │
│  ParameterTuningBridge (QObject)                        │
│    ├─ 解析 QML 调参配置                                  │
│    ├─ 循环: 生成参数 → 运行回测 → 收集结果                │
│    └─ 发射进度/完成信号                                  │
│        │                                                 │
│        ▼ (调用领域层)                                     │
├─────────────────────────────────────────────────────────┤
│                   Domain Layer (零 Qt)                    │
│  ┌──────────────────────┐  ┌──────────────────────┐     │
│  │ ParameterSpace        │  │ IOptimizer            │     │
│  │  - ParamRange[]       │  │  + optimize(…)        │     │
│  │  - generateGrid()     │  └──────────┬───────────┘     │
│  │  - randomSample()     │             │                 │
│  └──────────────────────┘  ┌──────────┴───────────┐     │
│                              │ GridSearchOptimizer   │     │
│                              │ BayesianOptimizer     │     │
│                              └──────────────────────┘     │
│  ┌──────────────────────────────────────────────────┐   │
│  │ OptimizationResult                                │   │
│  │  - TrialResult[] (sorted by objectiveValue desc)   │   │
│  │  - bestParams, bestMetrics, convergenceHistory     │   │
│  └──────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────┤
│                   Engine Layer                           │
│  StrategyEngine::backtest()  ←  Bridge 逐次调用          │
│  StrategyBacktestResult      →  Bridge 逐次收集          │
└─────────────────────────────────────────────────────────┘
```

**核心原则**：
1. 优化器（`IOptimizer`）是**纯算法**——不持有引擎引用，不知道回测存在
2. 桥接层提供 `std::function<double(const ParamSet&)>` 评估函数，内部调用 `StrategyEngine::backtest()`
3. 优化器只产出 `TrialResult`，排序/持久化/展示由桥接层负责

---

## 3. 领域层设计（`src/domain/optimization/`）

> 遵守 CLAUDE.md: 零 Qt 依赖 · 完全面向对象 · 私有数据成员 · 无字符串路由

### 3.1 参数范围定义

```cpp
// ParamDef.h
namespace domain::optimization {

enum class ParamType : std::uint8_t {
    Int = 0,
    Double = 1,
    Bool = 2,
    Enum = 3
    // FactorWeights = 4  // V2: 因子权重向量
};

/// 枚举选项: value → label
struct EnumOption final {
    int value{0};
    std::string label;
};

/// 单个参数维度的搜索范围
class ParamRange final {
public:
    // 数值构造
    static ParamRange intRange(std::string name, int min, int max, int step);
    static ParamRange doubleRange(std::string name, double min, double max, double step);

    // 布尔构造
    static ParamRange boolRange(std::string name);

    // 枚举构造
    static ParamRange enumRange(std::string name, std::vector<EnumOption> options);

    // 只读访问
    const std::string& name() const noexcept;
    ParamType type() const noexcept;
    int intMin() const;
    int intMax() const;
    int intStep() const;
    double doubleMin() const;
    double doubleMax() const;
    double doubleStep() const;
    const std::vector<EnumOption>& enumOptions() const;

    // 生成候选值列表
    std::vector<double> candidateValues() const;  // int/double → double, enum → int→double, bool → {0,1}

    // 候选值数量（用于估算搜索空间大小）
    int candidateCount() const;

private:
    std::string m_name;
    ParamType m_type;
    // Numeric
    int m_intMin{0}, m_intMax{0}, m_intStep{1};
    double m_doubleMin{0.0}, m_doubleMax{0.0}, m_doubleStep{1.0};
    // Enum
    std::vector<EnumOption> m_enumOptions;
};
```

### 3.2 参数空间

```cpp
// ParameterSpace.h
namespace domain::optimization {

/// 一组具体参数值
using ParamSet = std::unordered_map<std::string, double>;

/// 参数搜索空间
class ParameterSpace final {
public:
    explicit ParameterSpace(std::vector<ParamRange> ranges);

    // 获取子空间（按策略类型筛选）
    ParameterSpace subSpace(const std::vector<std::string>& paramNames) const;

    // 生成所有网格点（按 ranges 顺序笛卡尔积）
    std::vector<ParamSet> generateGrid() const;

    // 随机采样一个点（均匀分布）
    ParamSet randomSample() const;

    // 总组合数（网格搜索用）
    std::size_t totalCombinations() const;

    // 维度数
    int dimension() const noexcept;

    // 按名查找
    const ParamRange* find(const std::string& name) const;

    const std::vector<ParamRange>& ranges() const noexcept;

private:
    std::vector<ParamRange> m_ranges;
};

} // namespace domain::optimization
```

### 3.3 目标函数规格

```cpp
// ObjectiveSpec.h
namespace domain::optimization {

enum class ObjectiveMetric : std::uint8_t {
    SharpeRatio = 0,
    AnnualizedReturn = 1,
    CalmarRatio = 2,
    SortinoRatio = 3,
    ProfitFactor = 4,
    Custom = 5
};

/// 约束条件
struct OptimizationConstraint final {
    std::string metricName;  // "maxDrawdown", "totalTrades", "winRate"
    double minValue{0.0};    // metricValue >= minValue 才算合法
    double maxValue{std::numeric_limits<double>::max()};
    bool enabled{false};
};

/// 优化目标
class ObjectiveSpec final {
public:
    static ObjectiveSpec maximize(ObjectiveMetric metric);
    static ObjectiveSpec maximizeCustom(std::string metricName);

    ObjectiveMetric metric() const noexcept;
    const std::string& customMetricName() const;
    bool isMaximization() const noexcept;

    // 约束
    void addConstraint(OptimizationConstraint constraint);
    const std::vector<OptimizationConstraint>& constraints() const noexcept;

    // 从 StrategyBacktestResult 提取目标值
    double extractValue(const struct domain::strategy::StrategyBacktestResult& result) const;

    // 检查是否满足所有约束
    bool checkConstraints(const struct domain::strategy::StrategyBacktestResult& result) const;

private:
    ObjectiveMetric m_metric{ObjectiveMetric::SharpeRatio};
    std::string m_customMetricName;
    std::vector<OptimizationConstraint> m_constraints;
};

} // namespace domain::optimization
```

### 3.4 单次试验结果

```cpp
// TrialResult.h
namespace domain::optimization {

struct TrialResult final {
    ParamSet params;                                      // 参数名→值
    double objectiveValue{0.0};                           // 目标函数值
    bool constraintViolated{false};                        // 违反约束
    std::string errorMessage;                             // 回测失败原因
    std::int32_t trialIndex{0};

    // ⚠️ 约束违规约定:
    // 当 constraintViolated==true 时, objectiveValue 必须设为
    // -std::numeric_limits<double>::infinity()，
    // 确保排序后违规试验沉底，不会被误选为 bestTrial。
    //
    // 此转换由 ObjectiveSpec::checkConstraints() 调用方负责，
    // 或由 Bridge 层的 TrialEvaluator lambda 在构造 TrialResult 时统一处理。

    [[nodiscard]] bool isViable() const noexcept {
        return !constraintViolated && errorMessage.empty();
    }
};

/// 调优完整结果
struct OptimizationResult final {
    std::vector<TrialResult> trials;    // 按 objectiveValue 降序, 违规试验沉底
    int totalTrials{0};
    int failedTrials{0};
    int constraintViolations{0};
    double elapsedSeconds{0.0};

    bool isEmpty() const noexcept { return trials.empty(); }

    /// 首个 viable 试验 (非违规非失败中目标值最高)
    const TrialResult* bestTrial() const {
        for (const auto& t : trials) {
            if (t.isViable()) return &t;
        }
        return nullptr;
    }
};

} // namespace domain::optimization
```

### 3.5 优化器接口与实现

```cpp
// IOptimizer.h
namespace domain::optimization {

/// 评估器回调: 给定一组参数，返回目标值
/// 由桥接层注入 — 内部运行 StrategyEngine::backtest()
using TrialEvaluator = std::function<TrialResult(const ParamSet&, std::int32_t trialIndex)>;

/// 优化器抽象
class IOptimizer {
public:
    virtual ~IOptimizer() = default;

    /// @param space 参数搜索空间
    /// @param objective 优化目标
    /// @param evaluator 评估回调（桥接层注入）
    /// @param maxTrials 最大试验次数（0 = 不限制，网格搜索跑完所有组合）
    /// @param onProgress 进度回调 (completed, total)
    virtual OptimizationResult optimize(
        const ParameterSpace& space,
        const ObjectiveSpec& objective,
        const TrialEvaluator& evaluator,
        int maxTrials,
        const std::function<void(int, int)>& onProgress
    ) = 0;
};

/// 网格搜索
class GridSearchOptimizer final : public IOptimizer {
public:
    OptimizationResult optimize(…) override;
};

} // namespace domain::optimization
```

**设计关键点**：
- `IOptimizer::optimize()` 接受 `TrialEvaluator` —— 优化器不调用引擎，只调回调
- `TrialResult` 不含 `StrategyBacktestResult` —— 领域层零 Qt / 零引擎依赖
- 桥接层适配：把 `StrategyBacktestResult` 转成 `objectiveValue` 填入 `TrialResult`
- `maxTrials=0` 时网格搜索跑完所有组合，贝叶斯则到收敛

### 3.6 贝叶斯优化器（V2）

```
V1 只交付 GridSearchOptimizer，贝叶斯优化架构预留：

BayesianOptimizer:
  - 使用高斯过程（GP）作为代理模型
  - 核函数: Matern 5/2
  - 采集函数: Expected Improvement (EI) / GP-UCB
  - 前 5 + 2*dim 次随机采样作为初始设计
  - 迭代直到 maxTrials 或 EI < 1e-6
```

---

## 4. 参数空间 — 按策略类型的可调参数

### 4.1 通用参数（所有策略类型）

| 参数 | 类型 | 范围 | 默认 | 步长 |
|------|------|------|------|------|
| `maxPositions` | int | [5, 200] | 100 | 5 |
| `maxWeightPerStock` | double | [0.02, 0.50] | 0.10 | 0.02 |
| `minWeightPerStock` | double | [0.0, 0.10] | 0.0 | 0.01 |
| `weightScheme` | enum | EQUAL/CAP/SIGNAL/RISK_PARITY | EQUAL | — |
| `rebalanceFrequency` | enum | DAILY/WEEKLY/MONTHLY/QUARTERLY | DAILY | — |
| `allowShort` | bool | false/true | false | — |
| `stopLossPercent` | double | [3.0, 20.0] | 10.0 | 1.0 |
| `takeProfitPercent` | double | [10.0, 50.0] | 20.0 | 5.0 |
| `minHoldDays` | int | [0, 30] | 0 | 1 |

### 4.2 MultiFactor 策略（`behaviorKind=MultiFactor`）

| 参数 | 类型 | 范围 | 默认 | 步长 |
|------|------|------|------|------|
| `topN` | int | [10, 200] | 50 | 5 |
| `minCompositeScore` | double | [0.0, 3.0] | 0.0 | 0.1 |
| `sellThreshold` | double | [0.0, 1.0] | 0.2 | 0.05 |
| `sellRankMultiplier` | double | [1.0, 5.0] | 2.0 | 0.5 |
| `industryNeutral` | bool | false/true | false | — |
| `maxPositions` | int | [10, 200] | 30 | 5 |

> 因子权重（`factorIds` + `weights`）属结构化搜索，V2 用 `FactorWeightOptimizer` + 分组探索。

### 4.3 技术指标策略参数

#### TrendFollowing (DoubleMovingAverage)

| 参数 | 类型 | 范围 | 默认 | 步长 |
|------|------|------|------|------|
| `fastPeriod` | int | [3, 30] | 5 | 1 |
| `slowPeriod` | int | [10, 120] | 20 | 5 |
| `priceField` | enum | OPEN/HIGH/LOW/CLOSE | CLOSE | — |

#### MeanReversion (BollingerBand)

| 参数 | 类型 | 范围 | 默认 | 步长 |
|------|------|------|------|------|
| `period` | int | [10, 60] | 20 | 5 |
| `standardDeviationMultiplier` | double | [1.0, 4.0] | 2.0 | 0.5 |
| `entryThreshold` | double | [0.5, 3.0] | 1.0 | 0.5 |
| `exitThreshold` | double | [0.0, 1.0] | 0.2 | 0.2 |

#### Momentum (RsiMeanReversion)

| 参数 | 类型 | 范围 | 默认 | 步长 |
|------|------|------|------|------|
| `period` | int | [5, 30] | 14 | 1 |
| `oversoldLevel` | double | [15.0, 40.0] | 30.0 | 5.0 |
| `overboughtLevel` | double | [60.0, 85.0] | 70.0 | 5.0 |

#### TrendBreakout (TurtleBreakout)

| 参数 | 类型 | 范围 | 默认 | 步长 |
|------|------|------|------|------|
| `channelPeriod` | int | [10, 60] | 20 | 5 |
| `breakoutMultiplier` | double | [0.5, 3.0] | 1.0 | 0.5 |
| `atrPeriod` | int | [10, 40] | 20 | 5 |

#### Arbitrage (StatisticalPairTrading)

| 参数 | 类型 | 范围 | 默认 | 步长 |
|------|------|------|------|------|
| `lookback` | int | [10, 120] | 20 | 10 |
| `entryZScore` | double | [1.0, 3.5] | 2.0 | 0.5 |
| `exitZScore` | double | [0.0, 1.5] | 0.5 | 0.5 |

#### MachineLearning, EventDriven, HighFrequency

这些策略类型参数依赖外部数据（模型文件、事件源、盘口深度），V1 不纳入自动调优。架构预留扩展点。

### 4.4 参数空间工厂（`ParameterSpaceFactory`）

由策略 `behaviorKind` 动态生成默认可调参数列表，供 QML 配置面板渲染。

```cpp
// ParameterSpaceFactory.h
namespace domain::optimization {

class ParameterSpaceFactory final {
public:
    /// @brief 按策略行为类型返回默认可调参数范围
    /// @param behaviorKind 策略行为类型 (MultiFactor / TrendFollowing / ...)
    /// @return 该类型所有可调参数的默认范围 (名称 + 类型 + min/max/step)
    static std::vector<ParamRange> defaultRanges(
        domain::strategies::StrategyBehaviorKind behaviorKind);

    /// @brief 通用参数 (所有策略类型共有)
    static std::vector<ParamRange> commonRanges();

    /// @brief 估算搜索空间大小 (各维度 candidateCount 之积)
    static std::size_t estimateSearchSpace(const std::vector<ParamRange>& ranges);

    /// @brief 检查是否会组合爆炸 (超出 10000 组合)
    static bool wouldExplode(const std::vector<ParamRange>& ranges,
                             std::size_t threshold = 10000);
};

} // namespace domain::optimization
```

**实现映射**（对照第 4.1~4.3 节参数表）:

| behaviorKind | defaultRanges 包含 |
|-------------|-------------------|
| TrendFollowing | common + fastPeriod, slowPeriod, priceField |
| TrendBreakout | common + channelPeriod, breakoutMultiplier, atrPeriod |
| MeanReversion | common + period, standardDeviationMultiplier, entryThreshold, exitThreshold |
| Momentum | common + period, oversoldLevel, overboughtLevel |
| MultiFactor | common + topN, minCompositeScore, sellThreshold, sellRankMultiplier, industryNeutral |
| Arbitrage | common + lookback, entryZScore, exitZScore |
| MachineLearning | 仅 common (模型依赖外部文件) |
| EventDriven | 仅 common (事件源依赖外部数据) |
| HighFrequency | 仅 common (盘口深度依赖实时数据) |
| Custom | 仅 common |

桥接层暴露给 QML:
```cpp
// ParameterTuningBridge
Q_INVOKABLE QVariantList getTunableParams(int behaviorKindIndex) const;
// 返回: [{ "name": "topN", "type": "int", "min": 10, "max": 200, "step": 5,
//           "default": 50, "label": "选股数量" }, ...]
```

---

## 5. 桥接层设计（`src/ui/bridge/`）

> 遵守 CLAUDE.md: 只负责转发与调度，禁止业务逻辑

### 5.1 `ParameterTuningBridge`

```cpp
// ParameterTuningBridge.h
class ParameterTuningBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)
    Q_PROPERTY(int completedTrials READ completedTrials NOTIFY completedTrialsChanged)
    Q_PROPERTY(int totalTrials READ totalTrials NOTIFY totalTrialsChanged)
    Q_PROPERTY(double bestScore READ bestScore NOTIFY bestScoreChanged)

public:
    // QML 调用
    Q_INVOKABLE void startTuning(const QString& strategyId, const QVariantMap& tuningConfig);
    Q_INVOKABLE void cancelTuning();

    // tuningConfig 格式:
    // {
    //   "algorithm": "grid_search" | "bayesian",
    //   "objective": "sharpe_ratio" | "annualized_return" | "calmar_ratio" | ...,
    //   "maxTrials": 500,           // 0 = 网格搜索跑完所有组合
    //   "paramRanges": [             // 要调优的参数范围
    //     { "name": "topN", "type": "int", "min": 10, "max": 200, "step": 5 },
    //     { "name": "maxPositions", "type": "int", "min": 5, "max": 100, "step": 5 }
    //   ],
    //   "constraints": [             // 可选
    //     { "metric": "maxDrawdown", "max": 0.25 }
    //   ]
    // }

signals:
    void isRunningChanged();
    void completedTrialsChanged();
    void totalTrialsChanged();
    void bestScoreChanged();
    void tuningProgress(int completed, int total, double bestScore);
    void trialCompleted(int index, QVariantMap result);  // 单次回测完成
    void tuningCompleted(QVariantMap summary);           // 全部完成
    void tuningFailed(QString error);
    void tuningCancelled();

private:
    TrialResult runSingleTrial(const ParamSet& params, int trialIndex);
    // 1. 参数注入 → StrategyCreationParams
    // 2. 调用 StrategyEngine::backtest()
    // 3. 提取 objectiveValue + 约束检查 → TrialResult

    std::unique_ptr<domain::optimization::IOptimizer> m_optimizer;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_cancelRequested{false};  // 用户取消标志

    /// @brief TrialEvaluator 中注入取消检查:
    ///   if (m_cancelRequested) return TrialResult{.errorMessage = "cancelled"};
    /// 引擎侧: StrategyEngine::backtest() 接受可选的 cancelFlag 指针,
    ///   每日循环中检测 → 提前返回 partial result
};
```

#### 5.1.1 取消传播路径

```
用户点击"取消"
  → cancelTuning() 设置 m_cancelRequested = true
  → 当前正在运行的 backtest() 检测 cancelFlag
     (StrategyEngine 每日循环开头: if (cancelFlag && *cancelFlag) return partialResult;)
  → backtest() 提前返回, errorMessage = "cancelled"
  → TrialEvaluator 返回 constraintViolated=true 的 TrialResult
  → GridSearchOptimizer 收到后停止枚举后续组合
  → 已完成的试验保留, 未开始的丢弃
  → emit tuningCancelled() 或 tuningCompleted(partial=true)
```

#### 5.1.2 `StrategyEngine` 修改

```cpp
// IStrategyService.h 中的 backtest 签名变更:
StrategyBacktestResult backtest(
    const domain::backtest::BacktestRequest& req,
    factor::compute::BacktestDataService* dataSvc,
    const std::function<void(double)>& onProgress,
    const std::atomic<bool>* cancelFlag = nullptr  // 新增: 可选取消标志
);

// runBacktestLoop() 中的检测点 (每个交易日开始):
if (cancelFlag && cancelFlag->load(std::memory_order_relaxed)) {
    result.success = false;
    result.errorMessage = "Cancelled by user";
    return result;  // 保留已计算的部分结果
}
```

**注意**: `cancelFlag` 使用 `std::memory_order_relaxed` —— 取消是尽力而为的信号，不需要严格的内存顺序保证。最多延迟一个交易日（日频回测场景下可接受）。
```

### 5.2 桥接层职责边界

| 桥接层做 | 桥接层不做 |
|---------|-----------|
| 解析 QVariantMap → `ParameterSpace` + `ObjectiveSpec` | 实现搜索算法 |
| 创建 `TrialEvaluator` lambda，内部调 `StrategyEngine::backtest()` | 计算目标函数值（委托 `ObjectiveSpec::extractValue`）|
| 选 `GridSearchOptimizer` 或 `BayesianOptimizer` | 决定哪些参数可调（委托 `ParameterSpace`）|
| 发射 QML 信号 | 存储/管理回测结果（用 `OptimizationResult`）|

---

## 6. 数据流

### 6.1 端到端流程

```
1. QML 用户配置:
   - 选择策略 → strategyId
   - 选目标函数 → objective (Sharpe/Calmar/...)
   - 勾选要调优的参数 → paramRanges[]
   - 设最大试验次数 → maxTrials
   - 选算法 → grid_search

2. ParameterTuningBridge::startTuning():
   - QVariantMap → ParameterSpace (domain)
   - QVariantMap → ObjectiveSpec (domain)
   - 创建 GridSearchOptimizer
   - 构建 TrialEvaluator lambda:
     ┌──────────────────────────────────────┐
     │ [this, strategyId, baseRequest](     │
     │   const ParamSet& params,            │
     │   int trialIndex                     │
     │ ) -> TrialResult {                   │
     │   // 注入参数到 BacktestRequest       │
     │   auto req = applyParams(            │
     │     baseRequest, params);            │
     │   // 运行回测                         │
     │   auto result = engine->backtest(    │
     │     req, dataSvc, nullptr);          │
     │   // 提取目标值                       │
     │   return TrialResult{                │
     │     .params = params,                │
     │     .objectiveValue = objective      │
     │       .extractValue(result),         │
     │     .constraintViolated = !objective │
     │       .checkConstraints(result)      │
     │   };                                 │
     │ }                                    │
     └──────────────────────────────────────┘

3. optimizer.optimize(space, objective, evaluator, maxTrials, onProgress)
   → OptimizationResult

4. 结果排序 → QVariantMap → emit tuningCompleted()
```

### 6.2 参数注入路径（详细设计）

#### 6.2.1 `StrategyParamOverlay` 结构

```cpp
// 在 BacktestRequest.h 中新增
namespace domain::backtest {

/// 策略参数覆写 — 调优时仅覆写可变字段，不动 rule_profile / factor_overlay 等
struct StrategyParamOverlay final {
    std::optional<int> topN;
    std::optional<int> maxPositions;
    std::optional<double> maxWeightPerStock;
    std::optional<double> minWeightPerStock;
    std::optional<int> weightScheme;        // cast to WeightScheme enum
    std::optional<int> rebalanceFrequency;  // cast to RebalanceFrequency enum
    std::optional<bool> allowShort;
    std::optional<bool> industryNeutral;
    std::optional<double> stopLossPercent;
    std::optional<double> takeProfitPercent;
    std::optional<int> minHoldDays;

    // MultiFactor 专用
    std::optional<double> minCompositeScore;
    std::optional<double> sellThreshold;
    std::optional<double> sellRankMultiplier;

    // 技术指标策略专用
    std::optional<int> fastPeriod;
    std::optional<int> slowPeriod;
    std::optional<int> signalPeriod;     // RSI period / signal line
    std::optional<int> macdFast;
    std::optional<int> macdSlow;
    std::optional<int> macdSignal;
    std::optional<int> bbPeriod;
    std::optional<double> bbStdDev;

    // 枚举选项: 由 Bridge 层从 QML enum index 转换后赋值
    std::optional<int> priceFieldIndex;  // TechnicalPriceType

    [[nodiscard]] bool isEmpty() const noexcept;
};

} // namespace domain::backtest
```

#### 6.2.2 注入流程（三步）

```
1. Bridge 层: ParamSet → StrategyParamOverlay
   ParameterTuningBridge::applyOverlay(ParamSet) {
       StrategyParamOverlay overlay;
       for (auto& [name, value] : paramSet) {
           if (name == "topN")              overlay.topN = static_cast<int>(value);
           else if (name == "maxPositions") overlay.maxPositions = static_cast<int>(value);
           else if (name == "maxWeightPerStock") overlay.maxWeightPerStock = value;
           else if (name == "fastPeriod")   overlay.fastPeriod = static_cast<int>(value);
           // ... 其余映射
       }
       return overlay;
   }

2. BacktestRequest 装配: 模板 + overlay → 完整 req
   auto req = baseRequest;  // 从 strategyId 读出的模板
   req.paramOverlay = std::move(overlay);

3. StrategyEngine::backtest() 内部:
   ┌─ 从 DB 读取策略 → StrategyCreationParams (模板)
   ├─ 若 req.paramOverlay 非空:
   │    applyParamOverlay(creationParams, req.paramOverlay)
   │    // 直接覆写 creationParams 对应字段 (内存操作, 不写 DB)
   └─ StrategyBase::create(instanceId, creationParams) → 策略实例
```

#### 6.2.3 `applyParamOverlay()` 辅助函数

```cpp
// StrategyEngineFacade.cpp 内部 (匿名命名空间或私有方法)
void applyParamOverlay(domain::strategy::StrategyCreationParams& p,
                       const domain::backtest::StrategyParamOverlay& o) {
    if (o.topN)              p.topN = *o.topN;
    if (o.maxPositions)      p.maxPositions = *o.maxPositions;
    if (o.maxWeightPerStock) p.maxWeightPerStock = *o.maxWeightPerStock;
    if (o.minWeightPerStock) p.minWeightPerStock = *o.minWeightPerStock;
    if (o.weightScheme)      p.weightScheme = static_cast<WeightScheme>(*o.weightScheme);
    if (o.rebalanceFrequency) p.rebalanceFrequency = static_cast<RebalanceFrequency>(*o.rebalanceFrequency);
    if (o.allowShort)        p.allowShort = *o.allowShort;
    if (o.industryNeutral)   p.industryNeutral = *o.industryNeutral;
    if (o.stopLossPercent)   p.stopLossPercent = *o.stopLossPercent;
    if (o.takeProfitPercent) p.takeProfitPercent = *o.takeProfitPercent;
    if (o.minHoldDays)       p.minHoldDays = *o.minHoldDays;
    if (o.minCompositeScore) /* 暂存到 strategySpec */;
    if (o.sellThreshold)     /* 暂存到 strategySpec */;
    if (o.sellRankMultiplier)/* 暂存到 strategySpec */;
    if (o.fastPeriod)        p.fastPeriod = *o.fastPeriod;
    if (o.slowPeriod)        p.slowPeriod = *o.slowPeriod;
    if (o.signalPeriod)      p.signalPeriod = *o.signalPeriod;
    if (o.macdFast)          p.macdFast = *o.macdFast;
    if (o.macdSlow)          p.macdSlow = *o.macdSlow;
    if (o.macdSignal)        p.macdSignal = *o.macdSignal;
    if (o.bbPeriod)          p.bbPeriod = *o.bbPeriod;
    if (o.bbStdDev)          p.bbStdDev = *o.bbStdDev;
}
```

**关键保证**: `applyParamOverlay()` 是纯内存操作，不调用 `StrategyEngine::fromDb()` 重读数据库。整个调优过程中策略只从 DB 加载一次（模板），后续试验仅覆写 overlay。

---

## 7. 文件清单

### 7.1 新增文件

```
src/domain/optimization/                       ← 新模块
├── include/
│   ├── ParamDef.h                             ParamType 枚举 + EnumOption
│   ├── ParamRange.h                           参数范围定义
│   ├── ParameterSpace.h                       参数空间 + ParamSet alias
│   ├── ParameterSpaceFactory.h                策略类型 → 默认可调参数工厂
│   ├── ObjectiveSpec.h                        优化目标 + 约束
│   ├── TrialResult.h                          TrialResult + OptimizationResult
│   ├── IOptimizer.h                           IOptimizer 接口 + TrialEvaluator
│   └── GridSearchOptimizer.h                  网格搜索实现
├── src/
│   ├── ParamRange.cpp
│   ├── ParameterSpace.cpp
│   ├── ParameterSpaceFactory.cpp
│   ├── ObjectiveSpec.cpp
│   └── GridSearchOptimizer.cpp
└── test/
    ├── ParamRange_test.cpp
    ├── ParameterSpace_test.cpp
    ├── ObjectiveSpec_test.cpp
    └── GridSearchOptimizer_test.cpp

src/ui/bridge/
├── include/ParameterTuningBridge.h            桥接层 QObject
└── src/ParameterTuningBridge.cpp

src/app/Qml/components/Strategy/
└── Tuning/                                    调优配置 UI
    ├── ParameterTuningConfigPanel.qml          参数选择 + 算法配置
    └── ParameterTuningResultPanel.qml          结果展示 (排行榜+收敛曲线)

doc/
└── parameter_auto_tuning_design.md            本文档
```

### 7.2 修改文件

| 文件 | 改动 |
|------|------|
| `src/domain/backtest/include/BacktestRequest.h` | 新增 `StrategyParamOverlay` 子结构 |
| `src/domain/strategy/include/IStrategyService.h` | `backtest()` 签名增加 `cancelFlag` 参数 |
| `src/domain/strategy/src/StrategyEngineFacade.cpp` | `backtest()` 支持 overlay 覆写 + cancelFlag 检测 |
| `src/ui/bridge/include/StrategyBridge.h` | 暴露 `ParameterTuningBridge` 访问 |
| `src/app/Qml/qml.qrc` | 注册新 QML 文件 |
| `src/domain/CMakeLists.txt` | 新增 `optimization` 子目录 |
| `src/ui/bridge/CMakeLists.txt` | 新增 `ParameterTuningBridge.cpp` |

---

## 8. 实施阶段

### Phase 1: 领域层 + 网格搜索（V1）

1. `ParamRange` + `ParameterSpace` — 参数空间定义、网格生成
2. `ObjectiveSpec` — 目标函数提取、约束检查
3. `TrialResult` + `OptimizationResult`
4. `GridSearchOptimizer` — 笛卡尔积全枚举
5. 单元测试

### Phase 2: 桥接层 + 引擎适配

6. `StrategyParamOverlay` — 扩展 `BacktestRequest`
7. `applyParamOverlay()` — `StrategyEngineFacade` 内存覆写路径
8. `StrategyEngine::backtest()` — 支持 `cancelFlag` 取消检测
9. `ParameterTuningBridge` — QML 交互、参数注入、回测调度
10. 集成测试

### Phase 3: UI（V1 交付）

10. `ParameterTuningConfigPanel.qml` — 参数选择 + 范围配置
11. `ParameterTuningResultPanel.qml` — 结果排名 + 性能曲线对比

### Phase 4: 贝叶斯优化（V2）

12. `BayesianOptimizer` — GP 代理模型 + EI/UCB 采集
13. 对比测试：贝叶斯 vs 网格搜索在不同维度的效率

---

## 9. 设计决策记录

| 决策 | 理由 |
|------|------|
| 优化器不持有引擎引用，用 callback | 领域层零依赖、可单测 |
| `TrialResult` 不含 `StrategyBacktestResult` | 避免领域层碰引擎类型 |
| 扩展 `BacktestRequest` 而非新增顶层结构 | 保持现有 `backtest()` 签名稳定 |
| `maxTrials=0` = 跑完所有网格组合 | 自然的默认语义，不需要预先计算组合数 |
| 贝叶斯优化 V2 交付 | 先验证网格搜索的工程链路，再加复杂度 |
| 不调 `factorIds` / `weights` 在 V1 | 可变长度向量搜索是独立问题，用 `FactorWeightOptimizer` |

---

## 10. 风险与缓解

| 风险 | 缓解 |
|------|------|
| 网格搜索组合爆炸（10参数×每维5值=9.7M组合） | UI 前端 `wouldExplode()` 预估，超出 10000 次警告用户 |
| 每次回测重建引擎从 DB 读参数（慢） | Phase 2 新增 `applyParamOverlay()` 内存覆写路径，绕过 `fromDb()` 重读 |
| 贝叶斯在高维连续空间效果下降 | 默认维度 ≤ 10，超出建议先用网格粗搜再贝叶斯精搜 |
| 回测耗时不确定，用户等待焦虑 | 进度信号逐次发射 + 支持中途取消 (cancelFlag) |
| 取消后当前回测无法立即停止 | cancelFlag 每交易日检测一次，日频场景延迟 ≤ 1 天回测时间 |

---

## 11. 测试场景

### 11.1 领域层单元测试

| 测试项 | 验证内容 |
|--------|---------|
| `ParamRange::candidateValues()` | int/double/bool/enum 四类型生成候选值数量与值正确 |
| `ParamRange::candidateCount()` | 与 hand-calc 一致 |
| `ParameterSpace::generateGrid()` | 3 维各 2/3/4 候选 → 24 组合，值与顺序正确 |
| `ParameterSpace::totalCombinations()` | 与笛卡尔积理论值一致 |
| `ParameterSpace::randomSample()` | 采样值在各维合法范围内 |
| `ParameterSpace::subSpace()` | 子集筛选后维度/候选数正确 |
| `ObjectiveSpec::extractValue()` | 从 `StrategyBacktestResult` 提取 Sharpe/Calmar/Return 与实际计算一致 |
| `ObjectiveSpec::checkConstraints()` | maxDrawdown > 阈值 → false; 满足条件 → true |
| `GridSearchOptimizer` (mock evaluator) | 枚举所有组合、结果按 objectiveValue 降序、违规沉底 |
| `OptimizationResult::bestTrial()` | 首个 viable 试验 = 非违规中目标值最高 |
| `ParameterSpaceFactory::defaultRanges()` | 每个 behaviorKind 返回非空 ranges，名称与 4.1~4.3 表一致 |
| `ParameterSpaceFactory::wouldExplode()` | 3×5×4×10×2=1200 < 10000 → false; 10×10×10×10×10=100000 → true |

### 11.2 桥接层集成测试

| 测试项 | 验证内容 |
|--------|---------|
| `QVariantMap → ParameterSpace` | 所有 ParamType 正确转换 |
| `QVariantMap → ObjectiveSpec` | metric + constraints 正确 |
| `ParamSet → StrategyParamOverlay` | 各字段映射正确（含枚举 int→enum 转换） |
| `applyParamOverlay()` | `StrategyCreationParams` 仅被覆写字段变化，未指定字段保持模板值 |
| 取消流程 | `cancelTuning()` 后后续试验不执行，当前试验尽早返回 |
| 进度信号 | `tuningProgress(completed, total, bestScore)` 每次试验后发射，值与实际一致 |

### 11.3 端到端测试

| 测试项 | 验证内容 |
|--------|---------|
| 小网格完整调优 | MultiFactor 策略, 2 参数×3 候选 = 9 组合, 全跑完无异常 |
| 约束生效 | 设 maxDrawdown < 0.05（不可能满足），所有试验违规，bestTrial=nullptr |
| 参数覆写生效 | topN=10 vs topN=200 的目标值不同（策略行为确有差异） |

---

## 12. 并发与扩展预留

### 12.1 并行执行接口（V1 不实现，架构预留）

```cpp
// 在 TrialEvaluator 层面并行: Bridge 层用线程池并发调用 evaluator
// IOptimizer 不需要感知 —— 它只产 ParamSet，由 Bridge 并发 evaluate

class ParameterTuningBridge {
    // V2: 传入线程池, 同时跑 N 个回测
    void setParallelism(int maxConcurrentTrials);
private:
    std::unique_ptr<foundation::thread::ThreadPoolExecutor> m_threadPool;
};
```

### 12.2 结果序列化

```cpp
// OptimizationResult → JSON (用于持久化到 live.strategy_tuning_results 表)
namespace domain::optimization {
    std::string toJson(const OptimizationResult& result);
    OptimizationResult fromJson(const std::string& json);
}
// 表结构:
// CREATE TABLE live.strategy_tuning_results (
//   run_id        UUID PRIMARY KEY,
//   strategy_id   UUID REFERENCES live.strategy(strategy_id),
//   run_at        TIMESTAMPTZ DEFAULT now(),
//   algorithm     TEXT,           -- "grid_search" / "bayesian"
//   objective     TEXT,           -- "sharpe_ratio" / ...
//   total_trials  INT,
//   best_params   JSONB,          -- { "topN": 50, "maxPositions": 30, ... }
//   best_score    DOUBLE PRECISION,
//   all_trials    JSONB,          -- [{ trialIndex, params, score, violated }, ...]
//   elapsed_secs  DOUBLE PRECISION
// );
```
