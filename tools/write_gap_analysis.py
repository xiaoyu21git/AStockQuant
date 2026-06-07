# -*- coding: utf-8 -*-
import codecs

content = """# 缺口分析：优化版时序图 vs 当前代码实现

> 基于 `doc/优化版性能时序图.md` 文档内容，逐一对比当前项目中相关模块的代码实现。

## 1. ✅ 已就绪的模块（缺口小或无）

| 时序图元素 | 对应代码 | 状态 |
|-----------|---------|------|
| **数据服务：列式存储+内存映射** | `ParquetMarketDataView` — 从 Parquet 文件内存映射，按列只读访问 | ✅ 已实现 |
| **因子缓存：缓存检查与复用** | `SignalCache` — LRU 淘汰 + 内存限额；`FactorComputeEngine::generate()` 先查缓存再计算 | ✅ 已实现 |
| **内存预算预估** | `FactorComputeEngine` 中 `exceedsMemoryBudget()` 函数 | ✅ 已实现 |
| **超时治理** | `FactorComputeEngine` 中 `hasTimedOut()` 检查及部分结果返回 | ✅ 已实现 |
| **资源管控：分批/内存上限/降速** | `ResourceGovernor` — 分批次计算、内存水位监控、`Throttling`/`Suspended` 级别 | ✅ 已实现 |
| **策略引擎：批接口** | `IStrategySignalEngine::evaluateBatch()` — 声明了批量评估接口 | ✅ 接口已声明 |
| **回测流水线编排** | `BacktestPipelineOrchestrator` — 信号生成->风控->订单->撮合 各阶段组装 | ✅ 已实现 |

## 2. ⚠️ 部分就绪的模块（有缺口）

| 时序图要求 | 现状 | 缺口 |
|-----------|------|------|
| **并行线程池计算（逻辑核-2）** | `FactorComputeEngine` 的 `fillRawTensor()` 按 factor 顺序串行执行 `evaluateOnClose` | 缺少因子间并行调度，外层调度是串行的 |
| **float32 轻量数据类型** | 全部使用 `double`（8 字节），如 `SignalTensorBuffer::values` 为 `std::vector<double>` | 传输/存储未采用 `float32`，内存占用大 2 倍 |
| **策略引擎：向量化信号生成（无逐行循环）** | `IStrategySignalEngine` 接口存在，但实现中未确认是否为纯向量化信号生成 | 需检视具体策略实现确认是否避免逐行循环 |
| **回测引擎：矩阵运算一键计算损益** | `StagePipeline` 使用阶段式方法，非纯矩阵一次性 PnL | 回测引擎可能仍基于每日迭代撮合 |
| **因子分析（IC/IR/分层）** | `AnalysisModule` 存在，`FactorComputeEngine` 中调用了 `analysisModule_->analyze()` | 具体分析指标是否涵盖 IC/IR/分层需确认 |

## 3. ❌ 缺失或未实现的模块

| 时序图要求 | 现状 | 缺口 |
|-----------|------|------|
| **SIMD 向量化计算** | 无显式 SIMD intrinsic 或编译器向量化编译指示 | 缺少 SIMD 加速，依赖纯标量计算 |
| **实盘：单线程低延迟增量更新模式** | 无增量因子计算模式；`LiveTradingAdapter` 仅做参数转换 | 缺少增量计算分支，实时场景需独立实现 |
| **实盘：交易网关（OrderGateway）** | 无 OrderGateway 抽象层；实盘适配器只做了参数转换 | 缺少独立的订单网关模块处理实际委托 |
| **实盘：绑定固定 CPU 核心** | `ResourceGovernor` 明确不负责 CPU 亲和性 | 缺少平台级亲和性设置脚本 |
| **数据服务：按批次分块返回** | `ParquetMarketDataView` 全量映射整个文件，无按批次的切片返回 | 缺少批次数据切片/加载接口 |
| **信号矩阵使用 float32** | `SignalTensorBuffer::values` 是 `std::vector<double>` | 整体未使用 float32 压缩 |
| **实盘数据流推送** | 无实时数据流推送或增量数据接收机制 | 缺少数据流接口 |

## 4. 缺口优先级排序

```
优先级 HIGH（影响性能核心路径）：
1. float32 数据类型转换 -> 内存减半，缓存效力倍增
2. SIMD 向量化计算 -> CPU 计算吞吐量 2x-8x
3. 并行线程池调度（因子间并行）-> 多核利用率提升

优先级 MEDIUM（功能完善）：
4. 回测引擎：矩阵一次性损益计算 -> 消除每日迭代循环
5. 策略引擎：确认向量化信号生成确无逐行循环

优先级 LOW（实盘场景）：
6. 单线程低延迟增量更新模式
7. OrderGateway 交易网关抽象
8. CPU 亲和性设置脚本
9. 数据服务批次切片返回
```

## 5. 总结

当前代码在 **数据存储（列式 Parquet 内存映射）**、**缓存（LRU SignalCache）**、**资源管控（ResourceGovernor 分批/降速）** 方面已基本到位。主要缺口集中在：

| 层面 | 缺口 |
|------|------|
| **计算性能层** | SIMD 缺失、未用 float32、因子间并行串行 |
| **回测层** | 非纯矩阵损益计算 |
| **实盘层** | 增量更新、网关、CPU 绑定基本未实现 |
"""

with codecs.open('doc/优化版性能时序图_缺口分析.md', 'w', 'utf-8-sig') as f:
    f.write(content)

print('Done: gap analysis document written')