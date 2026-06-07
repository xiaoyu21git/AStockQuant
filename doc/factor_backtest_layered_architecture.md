# 因子回测 —— 分层架构调用合同

> 严格按时序图的箭头方向和层次关系定义。采用分层架构：越上层越简洁（协调），越下层越详细（执行）。

---

## 分层架构总览

```
┌──────────────────────────────────────────────────────┐
│ Layer 1: 入口层  FactorUI (FactorBacktestBridge)      │
│          职责: 协调调度，不读写数据、不算因子、不分析  │
│          输入: QML → factorIds, startDate, endDate    │
│          输出: QML ← backtestCompleted(result)        │
├──────────────────────────────────────────────────────┤
│ Layer 2: 调度层  Scheduler (ResourceGovernor)         │
│          职责: 内存上限、CPU亲和性、分批规划、降速    │
│          下调用: DataSvc (请求批次数据)                │
│          输入: 股票池 + 时间区间 + 因子配置            │
│          输出: 批次计划 (numBatches)                   │
├─────────────────┬────────────────────────────────────┤
│ Layer 3a: 数据层 │ Layer 3b: 计算层                    │
│ DataSvc          │ FactorEngine                       │
│ (DataService)    │ (FactorComputeEngine)              │
│ 列存+内存映射     │ 缓存+并行+SIMD                     │
│ 上家: Scheduler  │ 上家: FactorUI                     │
│ 下家: (无)       │ 内部: SignalCache                  │
│                  │       ParallelChunkScheduler       │
│                  │       SIMDAdapter                  │
├──────────────────┴───────────────────────────────────┤
│ Layer 4: 报告层  Reporter (AnalysisModule/AnalysisKernel)
│          职责: IC/IR/分层/多空/单调性/换手 汇总分析    │
│          上家: FactorUI (所有批次完成后调用)           │
│          输入: 汇总因子值 + closeView + tradingParams  │
│          输出: AnalysisReport → FactorUI               │
└──────────────────────────────────────────────────────┘
```

---

## 调用链 (自上而下，逐层深入)

### 阶段1: 任务提交 (Layer 1 → Layer 2)

```
FactorUI: 收到 QML 请求 (factorIds, startDate, endDate, runtimeParams)
  │
  └─→ Scheduler.submit(股票池, 时间区间, 因子配置)
        │
        ├─ Scheduler 内部: 设置内存上限 (< 70% 物理内存)
        ├─ Scheduler 内部: computeBatchCount(totalStocks, 500)
        └─ 返回: 批次计划 → FactorUI
```

### 阶段2: 分批处理循环 (Layer 2 → Layer 3a → Layer 3b)

```
for each batch in batches:
  │
  ├─ Scheduler → DataSvc: loadBatch(batchIndex, [open,high,low,close,volume])
  │     │  DataSvc 内部: Parquet列存读取 → mmap → ZSTD/LZ4解压
  │     └→ 返回 float32 行情矩阵 → FactorUI
  │
  ├─ FactorUI → FactorEngine: compute(行情矩阵, 缓存键)
  │     │  FactorEngine 内部:
  │     │    ├─ SignalCache.load(cacheKey) → 命中则直接返回
  │     │    ├─ ParallelChunkScheduler.dispatch(tasks)
  │     │    │   线程数 = hardware_concurrency - 2
  │     │    ├─ SIMDAdapter 每 chunk 向量化 (AVX2/NEON)
  │     │    └─ SignalCache.store(cacheKey, result)
  │     └→ 返回 float32 因子值矩阵 → FactorUI
  │
  └─ FactorUI 内部: 释放本批行情矩阵 + 中间因子值
```

### 阶段3: 汇总分析 (Layer 1 → Layer 4)

```
FactorUI: 所有批次因子值已汇总
  │
  └─→ Reporter.analyze(汇总因子值, closeView, tradingParams)
        │  Reporter 内部:
        │    ├─ AnalysisKernel.buildIcsSeriesSummary()  → Rank IC 时序
        │    ├─ AnalysisKernel.buildLongShortSeriesSummary() → 分层收益
        │    ├─ PerformanceMetricsAggregator.compute() → 多空夏普/单调性/换手
        │    └─ 复算时优先查 SignalCache (复用已缓存的因子值)
        └→ 返回 AnalysisReport → FactorUI → emit backtestCompleted → QML
```

---

## 层次间接口合同

| 层次 | 调用方 | 被调方 | 关键方法 | 关键输入/输出 |
|---|---|---|---|---|
| 1→2 | FactorUI | Scheduler | submit(task) | 股票池+日期+因子配置 → 批次计划 |
| 2→3a | Scheduler | DataSvc | loadBatch(idx, fields) | 批次号+字段列表 → float32矩阵(返给FactorUI) |
| 1→3b | FactorUI | FactorEngine | compute(matrix, cacheKey) | 行情矩阵+缓存键 → float32因子值(返给FactorUI) |
| 3b内部 | FactorEngine | SignalCache | load/store | 缓存键 → SignalSet |
| 3b内部 | FactorEngine | ParallelChunkScheduler | dispatch | 计算任务 → 向量化结果 |
| 3b内部 | ParallelChunk | SIMDAdapter | 向量运算 | AVX2/NEON 批量浮点运算 |
| 1→4 | FactorUI | Reporter | analyze(allFactorValues) | 汇总因子值+closeView → AnalysisReport(返给FactorUI) |
| 4内部 | Reporter | AnalysisKernel | buildIcsSeriesSummary / buildLongShortSeriesSummary | SignalSet → IC时序/分层收益 |

---

## 与当前代码的差距

| 合同 | 状态 | 说明 |
|---|---|---|
| 1→2 FactorUI→Scheduler | ❌ | Bridge 未调 ResourceGovernor，无分批 |
| 2→3a Scheduler→DataSvc | ❌ | 无按批按字段的列存读取，Bridge 手工加载全量 QVariantList |
| 1→3b FactorUI→FactorEngine | ❌ | Bridge 直接调 BaseFactor 逐日串行，绕过 FactorComputeEngine |
| 3b内部 SignalCache | ❌ | 已实现但闲置 |
| 3b内部 ParallelChunkScheduler | ❌ | 已实现但闲置 (Bridge 用单线程 WorkerPool) |
| 3b内部 SIMDAdapter | ❌ | 已实现但闲置 |
| FactorUI内部释放 | ✅ | RAII 局部析构 |
| 1→4 FactorUI→Reporter | ⚠️ | Bridge 直接调 AnalysisModule，但无批次汇总后统一分析 |