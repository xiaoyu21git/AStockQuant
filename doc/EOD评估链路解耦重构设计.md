# EOD 评估链路解耦重构 — 完整设计文档（定稿）

> 任务来源：用户要求"分析是否需要对当前 EOD 进行重构，详细设计，解耦/可读性/扩展性/通用性必须达标"
> 用户决策：①深度重构一次到位 ②自检提示用引擎日志+交易日志（不动 UI）③按六轮架构评审定案兑现 ADR-001~009
> 评审状态：**架构冻结，设计文档通过评审（2026-08-14），准予进入 P0 实施阶段**

---

## 1. Context（为什么做）

2026-08-14 实盘 14:50 EOD 评估因子全空：当日日K未入库 → 视图缺"今天"锚点 → 全市场无信号，链路断裂被伪装成 NoSignal。已临时修复（当日合成行注入，Facade:481-511）。复盘确定三原则：

1. **硬性失败必须严格提示并截断**（链断级问题不得伪装 NoSignal）
2. **不是所有缺失都截断**——通用性与冗余接纳（单股跳过、字段 ffill、数据源回退链）
3. **频率切换是"小改动"**——评估主链（因子→信号→闸门→订单生成→提交）与频率解耦

**分析结论：需要重构。** 证据：三份后处理拷贝（finalizeAndSubmit:1692-1793 / drainQueue:1196-1239 / liquidateAll:966-987）；链断静默（fetchTodayPrices 返回值丢弃 L1858、账户空→Skipped）；频率硬耦合（两硬分支、isDailyFrequency 硬编码 true）；drainQueue 死代码；线程安全缺陷三处；EOD 回调无注销。

**硬约束：回测零漂移**（2026-01-01 起年化 10.75% V9 基线逐项不漂移）——backtest/runBacktestLoop/stepBatch 零改动、step() 内部零改动、**回测路径 gmsdk 调用 L2552-2553 保持不动**。

---

## 2. 频率与周期模型（两条正交轴）

### 轴 1：BarPeriod（数据粒度/评估周期，一等配置项，ADR-002）

```cpp
enum class BarPeriod : uint8_t { Daily, Minute1, Minute5 };
```
决定：数据源链装配、视图粒度、因子缓存分区、调度触发策略、持久化去重键。`EvalRequest{tradingDay, isCompensation, BarPeriod period, const IPriceProvider* priceProvider}` 显式携带，**管道内零 `if (period == ...)` 分支**。

### 轴 2：调仓节奏（TriggerPolicy，日频层内 日/周/月 变体）

`RebalanceConfig` 改造：删 `isDailyFrequency`，改 `BarPeriod period{Daily}; int interval{1};`。日频层评估**每个交易日 14:50 都触发**（持仓监控/闸门每日必跑），是否"调仓"由 TriggerPolicy 判定：
- `DailyPolicy(intervalDays)`：现 m_rebalanceInterval 语义（每 N 交易日调仓）
- `WeeklyPolicy(weekdays)`：只在配置星期几调仓（周频）
- `MonthlyPolicy(dayOfMonth)`：只在每月配置日调仓（月频）

**周频/月频 = 日频层 + 换 TriggerPolicy + 换持久化键后缀，管道与数据源零改动。**

### 调度器矩阵

| 调度器 | 驱动 | 触发 | 持久化去重键 |
|---|---|---|---|
| `CronEvaluationScheduler`（日/周/月共用，改名自 DailyEodScheduler） | 30s 轮询 + gmsdk EOD 回调兜底 + 次日 0:00-9:30 补单窗口 | 交易日 [triggerMinute, +10min) 窗口（用户策略 14:50） | `lastEvalDay.<strategyId>.<period>` |
| `IntervalEvaluationScheduler`（1min/5min 实时，ADR-003：K线快照轮询驱动，非 tick 级回调） | 轮询步长 = 周期/2（1min→30s，5min→150s） | 周期边界 +kBoundaryOffsetSec(5s) | `lastEvalDay.<strategyId>.<period>`（存最近 bar 时间） |

两者实现公共接口 `IEvaluationScheduler{start/stop/setEvalCallback/setStrategyId/setTriggerPolicy}`；EvalFn 签名升级为返回 `EvalResult`；持久化全部走 AppStateStore。**IntervalEvaluationScheduler 本次落地为配置驱动框架**：仅当策略配置 `period=Minute1/5` 时实例化，当前策略全部 Daily → 不实例化（激活受分钟数据源阻塞）。

### 配置映射

fromDb 读 DB 策略参数（现有 rebalanceFrequency 机制扩展）：`period`（默认 Daily）、`interval`、`triggerPolicy`。缺省值逐项复刻现状行为。

---

## 3. 多频率协同模型（持仓/调仓/加仓/减仓/清仓如何配合）

### 层级角色（评审冻结：决策层与执行层分离，叠加强化非层层传递）

| 层级 | 功能 | 数据依赖 |
|---|---|---|
| **日频信号层（决策层）** | "今天收盘该不该调仓/换股/持有哪些票"——选股、配仓、调仓判定 | 日线数据（已闭合） |
| **分钟实时层（风控/执行层）** | 盘中持仓监控（止损/提前减仓/加仓）、下单时机优化 | 分钟数据 |

**⚠️ 明确禁止（ADR-008）：tick/分钟信号逐级向上传递到日频决策层。** 理由：①日频决策若依赖分钟信号，实盘与回测逻辑本质分歧——回测无分钟数据回放，10.75% 基线门禁被击穿；②违反 ADR-004 缓存物理隔离，引入跨频时序依赖与脏读；③日线调仓信号的核心是"选什么票、配多少仓"，用日线收盘价足够且抗噪。**日频决定"今天买不买"，分钟频决定"盘中要不要提前跑"。**

演进路径（仅记录为未来候选，本次不做）：分钟因子降频聚合为日线衍生特征（"今日 14:50 VWAP 偏离度"成为新的日线因子），前置：分钟级回测能力。

### 动作归属（双层架构落地）

| 动作 | 归属层 | 触发 | 路径 |
|---|---|---|---|
| **选股/调仓**（换仓买卖） | 日线层 Daily（决策） | 调仓日 14:50（TriggerPolicy 判定） | 管道 collectSignals → step → Finalizer |
| **持仓监控**（止损/减仓出场） | 日线层 Daily（决策） | **每交易日** 14:50（非调仓日也跑） | finalizeAndSubmit 持仓出场审核（ruleGate.positionAction Exit/Reduce + minHoldDays） |
| **加仓** | 日内层 Minute1/5（执行） | 周期边界触发，高频因子信号 | 同一管道 collectSignals（HighFreqFactor 定池）→ Finalizer |
| **减仓/止损** | 日内层 Minute1/5（执行） | 同上（高频负向信号/闸门 Reduce） | 同上 |
| **清仓**（强制） | 跨层：人工/风控 | UI liquidateAll / 择时 forceLiquidate / 熔断 | liquidateAll → Finalizer(mandatory=true)，不经规则闸门 |

### 冲突避免五机制

1. **单线程串行**：同一策略所有周期评估经 `m_post` 投递到**同一个**引擎专用线程（m_dedicatedExecutor）——日频与分钟评估天然串行，无并发写。
2. **共享账户 + strategyId 逻辑隔离**（ADR-001）：账户/现金全局共享（AccountEngine 单例）；订单生成时持仓视图按 strategyId 过滤。单策略实盘无行为变化。
3. **因子缓存物理分区**（ADR-004）：`RuntimeFactorSvc` 缓存按 `(strategyId, BarPeriod)` **物理分区**（每周期独立 map），跨频零共享。现缓存键 `(iid, dateBuf)` 改 `(iid, period, dateBuf)`。
4. **提交幂等去重**（ADR-006）：`app_state.json` 存 `lastBasket.<strategyId>.<period>{day, basketId}`；同一 (策略, 周期, 交易日) 已提交 → 跳过（**日志 `[Eval] 提交跳过: 当日已提交 basketId=...`，显式说明避免运维误判**）。日频与实时互不覆盖。**刻意行为变化**：补单窗口重评若当日已提交则不再二次下单（幂等化，防重复成交）。
5. **簿记校验**（ADR-005，见 §9 P5）：任何周期评估前校验内部账本与券商快照一致，防止"只开仓不平仓"式仓位失控。

---

## 4. 架构总览图

```
┌─────────────────────────── 触发层 ───────────────────────────┐
│ gmsdk EOD回调 ─┐                                            │
│ 30s轮询兜底   ─┼→ IEvaluationScheduler ──m_post──→ 引擎专用线程 │
│ 次日补单窗口 ──┘   ├ CronEvaluationScheduler(日/周/月,TriggerPolicy)
│ (分钟频:轮询)      └ IntervalEvaluationScheduler(1min/5min, 边界+5s)
└──────────────────────────────┬──────────────────────────────┘
                               ↓ doEvaluate (去重/缺口检测/持久化判定)
┌──────────────────────────────┴──────────────────────────────┐
│ StrategyEngine::evaluateEndOfDay（薄壳, 公开签名不变）          │
│   回测/监听器早退 → PriceProviderFactory::createProvider(period,mode)
│   → buildPipelineDeps → runEvaluation → EvalResult           │
└──────────────────────────────┬──────────────────────────────┘
                               ↓
┌───────────────────── SignalEvaluationPipeline（频率无关）─────┐
│ checkRebalance → preflight(P1-P5) → prepareContext →         │
│ fetchPrices → computeBreadth → evaluateGates →               │
│ collectSignals(stepFn) → finalizeAndSubmit                   │
│   （EvalSession 栈上按值贯穿; 零周期分支; 零 gmsdk）            │
└───────┬───────────────────────────────┬─────────────────────┘
        ↓ stepFn = StrategyEngine::step ↓ (内部零改动)
        │ (因子定池→信号→规则闸门)        │
┌───────┴───────────────┐   ┌───────────┴──────────────────────┐
│ 数据源层(工厂装配)      │   │ SubmissionFinalizer               │
│ Fallback{tickCache     │   │ 幂等去重→OrderGenerator→篮子      │
│   →liveData}        │   │ →journal→dispatchOrders→PositionBook│
│ (补单=Db单级)          │   │ (liquidateAll mandatory 复用)     │
└───────────────────────┘   └───────────┬──────────────────────┘
                                        ↓
                     EngineListenerAssembler(簿记挂钩一次性装配)
                     → BookKeepingListener(applyOrder+转发) → onOrders
┌─────────────────────── 支撑层 ───────────────────────────────┐
│ RuntimeFactorSvc: 缓存按(strategyId,period)物理分区;           │
│   computeFactor/warmUpCache 拆分; liveView() 返回 shared_ptr  │
│ PositionBook: 内存+dirty; flush()落盘; adopt双分支             │
│ AppStateStore: 唯一持久化写者(原子写)                          │
│ DbTradingCalendar: 实盘路径交易日历(回测路径gmsdk不动)          │
└──────────────────────────────────────────────────────────────┘
```

---

## 5. 类图（新增类职责与关键接口）

| 类 | 位置 | 职责 | 关键接口 |
|---|---|---|---|
| `EvalTypes.h`（值类型） | include/ | 评估域纯值类型 | `BarPeriod`; `EvalStage`; `EvalFailureKind`; `EvalResult{status,failureKind,failedStage,reason,三计数}`; `EvalContext`(view=shared_ptr); `EodDayBar`; `PriceData`; `GateResult`; `PendingOrder`; `EvalRequest{tradingDay,isCompensation,period,priceProvider}` |
| `IPriceProvider` | include/ | 数据源抽象 | `fetchPrices(symbols,endDateStr,period) → map<sym,EodDayBar>`; `sourceName()` |
| `DbDailyBarPriceProvider` | src/PriceProviders.cpp | 现补单分支 L1379-1393 迁移 | |
| `GmTickCachePriceProvider` | 同上 | 现盘中分支 L1401-1408 | |
| `LiveDataDailyBarPriceProvider` | 同上 | 新回退级（LiveData 无 preClose→涨跌停过滤自动跳过，语义安全） | |
| `FallbackPriceProvider` | 同上 | 链式组合，逐级 INFO 日志 `[Price] 源=... 命中=N/M` | |
| `MinuteBarPriceProvider` | 同上 | **接口位**，本次不实现数据源 | |
| `PriceProviderFactory` | include/ + src/ | **唯一**周期分支点（ADR-009②） | `createProvider(BarPeriod, EvalMode{Intraday,Compensation}) → unique_ptr<IPriceProvider>`; Daily盘中=Fallback链, 补单=Db单级; 只消费 isCompensation 标志，零时间逻辑 |
| `LiveViewPreparer` | src/ | 当日合成行（现 L481-511 迁移） | **纯函数** `prepareRows(repo, rawRows, endDate, policy)` 返回新行集合不修改原容器；仅 Daily 链调用；操作构造视图前的未发布数据 |
| `SignalEvaluationPipeline` | include/ + src/ | 评估主链（频率无关） | `run(req, Deps&) → EvalResult`; 私有阶段方法×8; Deps 注入引擎成员引用（stepFn/orderGenerator/orderListener/tradeJournal/ruleGate/timingGate/circuitBreaker/factorService/calendar/accountSnapshotFn/positionBook/liquidationBlocklist/positionEntryDates/onForceLiquidate/minHoldDays/strategyId/accountId） |
| `EvalSession` | 管道内部 | 一次评估全部可变状态 | 栈上按值贯穿；持有 view shared_ptr（禁 .get() 长期持有）；计数（gateRejected/limitRejected/positionExits/三计） |
| `SubmissionFinalizer` | include/ + src/ | 共享提交核心（收敛三份拷贝） | `submit(SubmissionRequest{rawOrders,positionQtyMap,strategyId,accountId,tradingDay,journalPrefix,mandatory}) → SubmissionResult{totalSubmitted,basketId,skipped}`; 幂等去重在 dispatchOrders **之前**（已提交→skipped 直接返回，不调 OrderGenerator） |
| `IEvaluationScheduler` | include/EvaluationScheduler.h | 调度抽象 | start/stop/setEvalCallback/setStrategyId/setTriggerPolicy; `EvalScheduleConfig{triggerMinute,triggerWindowMinutes,compensationEndMinute,pollIntervalSec=30,postTriggerWaitSec=3600,evaluatedIdleWaitSec=1800}`（P6: 时间参数零兜底—三时间默认哨兵 0, 配置缺失/非法拒绝启动; triggerWindowMinutes 配置化） |
| `CronEvaluationScheduler` | src/ | 日/周/月共用 | TriggerPolicy 变体; setEodTriggerTime（trading_connection.json 覆盖）; setDataSyncDayProvider(optional<int>); **stop() 真注销回调 token**; start() 时按需调 warmUpCache |
| `IntervalEvaluationScheduler` | src/ | 1min/5min 轮询 | 步长=periodMinutes*60/2; 边界+kBoundaryOffsetSec=5 |
| `ITradingCalendar` | include/ | 交易日历抽象（实盘路径） | `currentTradingDay()`; `previousTradingDay(date)` |
| `DbTradingCalendar` | src/infrastructure/database/ | Facade L855-889 lambda 逻辑**完全迁移**（含非交易日判定/节假日分支） | 替换 checkRebalanceDay L1336-1337 与 countTradingDaysBetween L1319-1320；**回测 L2552-2553 不动** |
| `PositionBook` | include/ + src/ | 内部持仓账本（ADR-005） | `applyOrder(o)`（内存+dirty，不逐单落盘）; `checkAgainstBroker(snap)`; `flush()`（优雅关闭必调，可选30s兜底）; adopt 双分支（§9） |
| `BookKeepingListener` | 装配器内 | 委托包装 | 持原 m_orderListener；onOrders 先 applyOrder 再转发；无需卸载 |
| `EngineListenerAssembler` | include/ + src/ | 簿记挂钩一次性装配（ADR-009①） | `install()` 于引擎初始化时包装 m_orderListener，**onOrders 调用点零改动** |
| `AppStateStore` | src/infrastructure/include/database/ | 唯一持久化写者 | `forPath(path)` 路径键控单例+mutex+tmp/rename 原子写; `readInt/writeInt(section,key)` |

### 现有类职责变化

| 类 | 变化 |
|---|---|
| `StrategyEngine` | 删 drainQueue/队列三件套/droppedTicks/isDailyFrequency/m_isDailyFrequency/RebalanceConfig::isDailyFrequency/fromDb L367 硬编码/startLiveLoop 分支；EodContext 等 5 嵌套结构迁出；evaluateEndOfDay 薄壳化；新增 private runEvaluation（返回 EvalResult）+ buildPipelineDeps；liquidateAll 提交段改 Finalizer；新增成员 m_pipeline/m_positionBook/m_calendar |
| `RuntimeFactorSvc` | liveView() 改 shared_ptr 发布句柄；缓存按 (strategyId, period) 物理分区；+probeFactorCrossSection；私有 computeFactor/warmUpCache 拆分 |
| `IRuntimeFactorService` | +probeFactorCrossSection 纯虚；liveView() 返回类型改 shared_ptr |
| `MarketDataService` | registerEndOfDayCallback 返回 token + 新增 unregister |
| `PostMarketSyncService` | 持久化改 AppStateStore（键 dataSyncDay 不变） |
| `StrategyBridge` | 仅 setupLiveMarketView 悬垂修复（make_shared 移交引擎持有 m_injectedLiveView） |

---

## 6. 时序图

### 6.1 日频评估一次（14:50 实盘 Live）

```
14:50:xx  轮询线程 schedulePollCheck (30s粒度)
            → 命中 [890, 900) 分钟窗口 → m_post → 引擎专用线程
doEvaluate(tradingDay):
  ├─ 去重: lastEvalDay.<id>.<period> 已评 → skip
  ├─ K线缺口检测: AppStateStore readInt("dataSyncDay") → nullopt 则跳过告警(INFO)
  └─ m_evalFn(tradingDay, isCompensation=false)
Facade::evaluateEndOfDay (薄壳):
  ├─ m_isBacktestMode → Skipped  |  !m_orderListener → Skipped   (早退保持)
  ├─ PriceProviderFactory::createProvider(Daily, Intraday)   ← EvalMode 由调度器标志决定
  ├─ buildPipelineDeps(req) → Deps
  └─ m_pipeline->run(req, deps)
       checkRebalance: calendar->previousTradingDay (DB) → 非调仓日 Skipped
       preflight P1-P5: 视图→锚点/因子→账户→价格→簿记   (首败 → Error 截断)
       prepareContext: 符号/日期/列映射 (原 L1350-1371)
       fetchPrices: Fallback{tickCache→liveData} 逐级日志 → 空 → Error(PriceDataEmpty)
       computeBreadth: 视图 close 末行 MA60/20 + prices.bars 当日 above 计数
       evaluateGates: 宽度冻结(<=0.35) + 规则闸门 + 择时(000300.SH MA20/60)
       [forceLiquidate && !熔断] → deps.onForceLiquidate() (=liquidateAll) → Submitted
       collectSignals: 逐 symbol stepFn(mdp) → 涨跌停过滤(计数) → PendingOrder
                       (单股无价/step异常 → WARN+continue)
       finalizeAndSubmit: 规则信号审核(计数) → 持仓出场审核(最少持有期)
         → SubmissionFinalizer.submit:
             幂等去重(已提交→skipped) → OrderGenerator.generate → tagBasketOrders
             → journal("提交 买入 ...") → dispatchOrders(SemiAuto拦截/Live直发)
             → PositionBook.applyOrder (内存+dirty)
  └─ journal "日终 持仓/净值" (原 L1796-1802 保持)
doEvaluate 持久化: 非补单 Submitted/NoSignal/AllRejected/Skipped → lastEvalDay 写入
                  Error → 不写, WARN "等待补单重试"
```

### 6.2 补单窗口（次日 0:00-9:30）

```
0:00-9:30  CompensationWindow → isCompensation=true
  → Provider = DbDailyBar 单级 (空 → Error(PriceDataEmpty), 不静默)
  → 幂等去重: 昨日 14:50 已提交 → "[Eval] 提交跳过: 当日已提交 basketId=..."
  → 未提交 → 正常评估提交
```

### 6.3 分钟频评估（未来示意，本次仅 seam）

```
盘中周期边界+5s (IntervalEvaluationScheduler, 轮询 周期/2)
  → doEvaluate → Facade::evaluateIntraday (未来薄壳)
      ├─ Provider 链: MinuteBarProvider(未来) → liveData
      ├─ 分钟视图构建器 (HighFreqFactor close_minute/volume_minute 接入点)
      └─ 同一 SignalEvaluationPipeline::run — 主链零改动 (G3 验证)
```

---

## 7. 13 环迁移检查表（逐环对照，顺序/日志/早退条件不变）

| # | 现位置 | 目标 | 迁移要点 | 行为差异 |
|---|---|---|---|---|
| 1 | checkRebalanceDay L1330-1348（gmsdk L1336-1337） | 管道 checkRebalance | `::get_previous_trading_date` → calendar->previousTradingDay | 无（DB 日历与调度器注入同源）；回测路径 L2552-2553 不动 |
| 2 | countTradingDaysBetween L1313-1328（gmsdk L1319-1320） | 管道内 | 同步换 calendar | 无 |
| 3 | 回测早退 L1842-1845 | run 入口早退 | 保持 | 无 |
| 4 | 无监听器早退 L1846-1849 | run 入口早退 | 保持 | 无 |
| 5 | prepareEodContext L1350-1371 | prepareContext | 视图空 WARN+false → **Error(ViewEmpty)** | 升级 |
| 6 | fetchTodayPrices L1373-1414 | fetchPrices | 两硬分支 → Provider 链；**返回值不再丢弃** | 空 → Error(PriceDataEmpty)（原静默） |
| 7 | computeMarketBreadth L1416-1455 | computeBreadth | 原样迁移 | 无 |
| 8 | 账户快照 L1864-1870 | preflight P3 | totalAsset<=0 Skipped → **Error(AccountEmpty)** | 升级 |
| 9 | posQtyMap 构建 L1871-1873 | prepareContext 阶段 | 原样；按 strategyId 过滤持仓（ADR-001） | 单策略无变化 |
| 10 | EventRiskSubscriber L1874-1875 | preflight 通过后 | 原样保留位置 | 无 |
| 11 | evaluateEodGates L1457-1527 | evaluateGates | 原样；forceLiquidate 分支 L1879-1882 → deps.onForceLiquidate | 无 |
| 12 | collectEodSignals L1529-1603 | collectSignals | step() → deps.stepFn；涨跌停过滤 L1579-1580 **增加计数** | 计数仅入日志/状态 |
| 13 | finalizeAndSubmit L1605-1828 | finalizeAndSubmit + SubmissionFinalizer | 规则信号审核 L1615-1635（计数）保留在管道；提交段 L1692-1793 → Finalizer；Top-3 日志/QtyDiag 保留；账户快照插入 L1816-1824 与日终持仓日志 L1796-1802 保持；状态判定 L1826-1828 改写（§8） | 状态语义兑现 |
| — | liquidateAll L940-990 | 订单构造保留 + Finalizer(mandatory=true) | L966-987 提交段 → Finalizer | journalPrefix="清仓" |
| — | drainQueue L1151-1257 | **删除** | m_mdpQueue 零 push 调用者；删除后 m_dedicatedExecutor 循环无任何 m_mdpQueue 等待 | 死代码移除 |

---

## 8. 异常矩阵（注入 → 检测 → 提示 → 持久化 → 恢复）

| 注入场景 | 检测点 | Error kind | 引擎日志（ERROR） | 交易日志（journal） | lastEvalDay 持久化 | 恢复路径 |
|---|---|---|---|---|---|---|
| 视图空/symbols 空 | P1 | ViewEmpty | `[Eval] <day> 自检失败 kind=ViewEmpty stage=Preflight` | `<day> 自检失败 kind=ViewEmpty` | **不写** | 次日补单窗口重试 |
| 锚点缺失（视图末行<评估日） | P2 | AnchorMissing | 同上模板 | 同上 | 不写 | 次日补单重试 |
| 因子全截面无有限值 | P2 | FactorSnapshotEmpty | 同上 | 同上 | 不写 | 次日补单重试 |
| 账户 totalAsset<=0 | P3 | AccountEmpty | 同上 | 同上 | 不写 | 次日补单重试 |
| 价格全空（链全空） | P4 | PriceDataEmpty | 附 `源链=tickCache\|liveData` | 同上 | 不写 | 次日补单重试 |
| 账本 vs 券商股数偏差 | P5 | BookKeepingMismatch | 附 symbol/账本/券商 明细 | 同上 | 不写 | **人工介入** |
| Provider 抛异常 | 管道 catch | Exception | `[Eval] 未捕获异常 <what>` | 同上 | 不写 | 次日补单重试 |
| 单股无价 | collectSignals 循环内 | 无（WARN+continue） | `[Eval] 单股无价跳过 sym=...` | — | 正常评估 | 原则②冗余接纳 |
| step 单股异常 | 循环内 catch | 无（WARN+continue） | `[Eval] step 异常跳过 sym=...` | — | 正常评估 | 原则②冗余接纳 |
| 信号全部被拒 | finalize 判定 | **AllRejected**（非 Error） | `[Eval] 完成 status=AllRejected` | `<day> 全部拒绝 生成=N 规则闸门拒绝=N1 涨跌停过滤=N2 生成器过滤=N3` | **正常写** | 次日正常评估 |
| 正常无信号 | finalize 判定 | NoSignal | `[Eval] 完成 status=NoSignal` | — | 正常写 | 次日正常评估 |
| 当日已提交（补单重评） | Finalizer 去重 | 跳过（skipped） | `[Eval] 提交跳过: 当日已提交 basketId=...` | — | 保持 | 幂等 |

**判定规则**（改写 L1826-1827）：生成>0 且提交=0 → AllRejected（totalGenerated 含涨跌停过滤前+规则闸门拒绝前的原始信号）；生成=0 提交=0 → NoSignal；提交>0 → Submitted；链断 → Error。**因子空不再落 NoSignal——被 P2 截断为 Error。**

---

## 9. 前置自检 preflight 精确表（顺序执行，首败即截断）

| # | 检查 | 失败 → |
|---|---|---|
| P1 | liveMarketView() 空或 symbols 空 | Error(ViewEmpty) |
| P2 | 视图末行日期 < 评估日 → 锚点缺失；因子策略 probeFactorCrossSection 全截面无有限值 | Error(AnchorMissing / FactorSnapshotEmpty) |
| P3 | 账户快照 totalAsset<=0（现为 Skipped，升级） | Error(AccountEmpty) |
| P4 | Provider 链 fetchPrices 空 map（现被静默丢弃，修复） | Error(PriceDataEmpty) |
| P5 | PositionBook 与券商快照股数偏差 > kBookToleranceShares(=0，常量占位未来可配置)，且偏差不能归因于未确认成交订单（豁免锚定提交日：账本−快照 == Σ未确认订单股数 即豁免；成交在快照体现后转正式，见附录 C3） | Error(BookKeepingMismatch) |

**adopt 判定双分支（终审澄清 3.4 定案）**：①app_state.json **无** `positionBook.<strategyId>` 键 → adoptBrokerSnapshot（首启信任券商，无 Error）；②键**存在**（即使映射为空）→ 不 adopt，直接 P5 校验（账本 0 vs 券商有持仓 = 偏移，必须 Error 人工介入）。

**持久化频率（终审建议 4.3 采纳）**：applyOrder 只更新内存 + dirty 标志，不逐单落盘；`flush()` 显式写盘（引擎优雅关闭前必调；可选 30s 定时兜底）。

**记账钩子集中装配（终审澄清 3.3 定案）**：`EngineListenerAssembler` 于引擎初始化一次性包装 m_orderListener 为 `BookKeepingListener`——**委托模式**：持原指针，onOrders 先 applyOrder 再转发，外部已持有引用不受影响、无需卸载；所有 onOrders 出口（dispatchOrders 实盘分支 + confirmBasket）**调用点零改动**。

**probeFactorCrossSection 与预热拆分（终审 4.1 定案）**：RuntimeFactorSvc 私有 `computeFactor(iid, date)`（纯计算）与 `warmUpCache(strategyId, period, symbols)`（纯预热）拆分；probe 逐 iid 锚点日调 computeFactor，只返回"全截面是否有有限值"，无"顺带"副作用；**预热由调度器 start() 按需显式调用**（不做引擎全局预热）；管道内绝不出现"预热"字样。

---

## 10. 分阶段实施（每阶段独立编译；评审门禁为硬性准入）

| 阶段 | 内容 | 评审门禁 / 验证门槛 |
|---|---|---|
| **P0 基础件**（纯新增零风险） | EvalTypes.h（含 BarPeriod）；IPriceProvider 四实现 + PriceProviderFactory + LiveViewPreparer（纯函数）；AppStateStore + 双服务持久化切换（JSON 合并逐键等价）；dataSyncDay optional 修复 | 门禁：EvalTypes 含 BarPeriod；fetchPrices 签名含 period；createProvider(period, mode) 存在且 Facade 零周期分支；EvalMode 枚举 {Intraday, Compensation}。验证：构建；app_state.json 键不变 |
| **P1 调度器** | IEvaluationScheduler + CronEvaluationScheduler（TriggerPolicy 日/周/月）+ IntervalEvaluationScheduler；EvalFn 改 EvalResult；回调 token+unregister；Facade 接线；PositionBook 落地 | 门禁：1min/5min 触发间隔与周期对齐（30s/150s，边界 kBoundaryOffsetSec=5）；持久化键含 period 维度。验证：构建；14:50 触发/去重/持久化与重构前一致 |
| **P2 管道提取+自检语义**（核心） | SignalEvaluationPipeline/EvalSession/SubmissionFinalizer 落地；evaluateEndOfDay 薄壳化；preflight P1-P5 上线；AllRejected 三计数明细；liquidateAll 提交段改 Finalizer；簿记挂钩 installBookKeepingHook 一次性装配；computeFactor/warmUpCache 拆分。**step() 内部零改动** | 门禁：Preflight 含 P5；管道内 `if (period==)` 为零；onOrders 调用点零改动；Deps 仅 buildPipelineDeps 构造；幂等去重在 dispatchOrders 之前。验证：构建；test_eod_order_flow 异常注入全用例；回测基线不漂移 |
| **P3 线程安全+缓存分区** | liveView() shared_ptr 化；buildLiveView 发布顺序；Bridge 悬垂修复；因子缓存按 (strategyId, period) 物理分区 | 门禁：跨频共享为零；EvalSession 持 shared_ptr 以引用传递，禁 .get() 长期持有。验证：构建；视图注入后 14:50 评估正常；审查无裸指针残留 |
| **P4 死代码+日历** | drainQueue 全链路删除（删除后 m_dedicatedExecutor 无 m_mdpQueue 等待）；ITradingCalendar/DbTradingCalendar 替换实盘路径两处 gmsdk（Facade L855-889 lambda 完全迁移）；**回测 L2552-2553 不动** | 门禁：`grep drainQueue\|m_mdpQueue\|isDailyFrequency` 零命中。验证：构建；回测基线；14:50 检查点 |

顺序理由：P0/P1 零行为风险先行（AppStateStore 修复磁盘竞态）；P2 是唯一"刻意"行为变化（Error 语义 + 幂等去重）单独成段；P3 独立；P4 必须等 P2 验证通过（先有新承载再拆旧桥）。

---

## 11. 验证方案

1. **回测判据（硬门禁）**：2026-01-01 起 V9 基线年化 10.75% 逐项比对。依据：backtest/stepBatch/step() 零改动、L2552-2553 gmsdk 保持。
2. **实盘 14:50 检查点（日志断言顺序）**：`[Eval] 开始` → `preflight 通过` → `[Price] 源=tickCache 命中=N` → 宽度 → 择时 → 信号 TOP3 → `完成 status=...`；journal 出现 `提交`/`全部拒绝`/`冻结` 行；app_state.json lastEvalDay/lastBasket 更新。
3. **异常注入**（扩展 tests/test_eod_order_flow.cpp，补 injectTestAccount 0 资产用例）：按 §8 异常矩阵逐行注入——视图空/锚点缺失/因子快照空/账户空/价格全空/簿记偏差 → Error+ERROR 日志+journal 行+不持久化；单股无价 → 跳过不截断；信号全拒 → AllRejected+三计数明细；tick 空 liveData 有值 → 走回退链成功；同 (策略,周期,日) 二次评估 → 幂等跳过。
4. **死代码断言**：grep `m_mdpQueue/drainQueue/droppedTicks/m_isDailyFrequency/kMaxQueueSize` 零命中。
5. **CLAUDE.md 审查**：零 Qt、OOP、private+getter、camelCase/m_/k 常量、无魔法数字（900/570/0.35/0.02/kBoundaryOffsetSec 落常量）、无字符串路由（failureKind 枚举）、无兼容回退。

---

## 12. 明确不做

1. 高频因子激活：MinuteBarPriceProvider 数据源实现、分钟视图构建器、HighFreqFactor 分钟字段接入、策略池 tick 自动订阅——只留接口位与配置驱动框架
2. **跨频信号传递逻辑**（ADR-008）：分钟信号修正日频决策、日频管道消费分钟数据——任何形式都不实现
3. UI 任何改动（状态不传 UI、无新桥接接口、BasketConfirmDialog 不动）
4. atrPercent 真实 ATR 计算（仍 0.02 常量化占位）
5. 回测与 EOD 合流（回测继续走 stepBatch）
6. 周频/月频策略实际配置启用（TriggerPolicy 框架落地，当前策略仍日频）
7. prepareMarketData 分层重构；LiveData 补写 turnover/preClose 字段

---

## 13. ADR 正文（评审冻结，实施必须兑现）

| ADR | 内容 |
|---|---|
| 001 | 共享账户 + strategyId 逻辑隔离，不拆物理资金账号 |
| 002 | BarPeriod 一等配置项，EvalRequest 显式传递，管道内零分支 |
| 003 | 实时=K线周期快照评估，轮询驱动（周期/2 步长，边界+5s），非 tick 级回调 |
| 004 | 因子缓存按 (strategyId, BarPeriod) 物理分区，禁用跨频共享 |
| 005 | Preflight P5 簿记校验（PositionBook vs 券商快照，偏差>0 且非 in-flight → Error 截断） |
| 006 | 提交去重键绑定 (strategyId, BarPeriod)，存 app_state.json lastBasket |
| 007 | 核心管道命名 SignalEvaluationPipeline（EodEvalTypes→EvalTypes、EodEvalRequest→EvalRequest） |
| 008 | **禁止跨频信号向上传递**：日频决策不消费分钟/tick 信号；两层并行独立评估。未来需分钟修正日频时走"分钟因子降频聚合为日线特征"路径（前置：分钟级回测能力，本次不做） |
| 009 | **装配逻辑集中化（无散落逻辑）**：①簿记挂钩经 EngineListenerAssembler 一次性装配，onOrders 调用点零改动；②显式 PriceProviderFactory::createProvider(period, mode)，Facade 零周期分支；③computeFactor/warmUpCache 拆分，probe 无"顺带"副作用；④Deps 仅由 buildPipelineDeps 一处构造 |

---

## 14. 关键文件

- `src/domain/strategy/src/StrategyEngineFacade.cpp` — evaluateEndOfDay 薄壳化、13 环迁出、drainQueue 删除、startLiveLoop 去分支
- `src/domain/strategy/include/IStrategyService.h` — 成员裁剪、RebalanceConfig 改造、IRuntimeFactorService 接口演进（+probeFactorCrossSection）
- `src/domain/strategy/{include,src}/DailyEodScheduler.*` → EvaluationScheduler.*（+IntervalEvaluationScheduler）
- `src/domain/strategy/{include,src}/RuntimeFactorSvc.*` — 视图 shared_ptr 化、缓存按 period 物理分区、probeFactorCrossSection
- `src/domain/market/{MarketDataService.h,.cpp}` — 回调 token 注销
- `src/ui/bridge/src/StrategyBridge.cpp` — 仅 setupLiveMarketView 悬垂修复
- `src/domain/strategy/CMakeLists.txt` + infrastructure CMakeLists — 新文件登记

## 15. 交付流程（终审后续流程定案）

1. 本文件即设计文档定稿（已落 `doc/EOD评估链路解耦重构设计.md`），随后启动 P0 代码编写。
2. 每阶段完成后按门禁 MR 审查，**门禁不通过不得合入**。
3. P2 阶段完成后（管道提取+Error 语义+簿记校验上线）进行中期代码走查。
4. 交付前走查完整行为路径（fix-verify-behavior-path 约定）。
5. 设计澄清附录 C1-C5 已获终审确认（2026-08-14），见文末附录。

---

## 附录：设计澄清 C1-C5（评审确认 2026-08-14）

| # | 歧义点 | 裁定 |
|---|---|---|
| C1 | EvalRequest.priceProvider 生命周期 | Facade 栈持有 unique_ptr，EvalRequest 传裸指针，仅同步 run() 栈帧内有效，管道不得缓存 |
| C2 | probe 单股 computeFactor 异常 | catch+WARN+该股计无有限值，继续下一只；仅全截面无有限值才返回 false → Error(FactorSnapshotEmpty) |
| C3 | P5 in-flight 豁免期 | 锚定提交日；偏差可完全归因于未确认成交订单即豁免（账本−快照 == Σ未确认订单股数）；成交在快照体现后转正式。周五提交、周一未确认成交 → 仍豁免 |
| C4 | LiveViewPreparer 非交易日 | no-op 不报错：输入行集合原样返回，合成行为空，不产生 Error |
| C5 | 补单窗口 TriggerPolicy | 以被补单交易日（评估日 tradingDay 参数）为基准重新计算，纯函数结果与昨日一致，零新增状态 |

完整裁定与依据见 `doc/设计澄清.md`。

**实施纪律门禁（纳入各阶段 MR 审查）**：主文档与本附录未覆盖的分支/条件，实施方**禁止自行推断**，须先记录澄清获评审确认后方可编码。
