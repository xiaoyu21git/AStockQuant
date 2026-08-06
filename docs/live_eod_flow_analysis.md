# 实盘 EOD 下单全链路 — 最终版

> 生成时间: 2026-08-07 | 包含所有重构和修复

---

## 一、全链路总览

```mermaid
flowchart TD
    subgraph A["启动阶段"]
        A1["UI: StrategyBridge::start()"] --> A2["StrategyManager::startStrategy()"]
        A2 --> A3["StrategyEngine::fromDb()"]
        A3 --> A4["prepareMarketData()"]
        A4 --> A5["startLiveLoop()"]
        A5 --> A6["DailyEodScheduler::start()"]
    end

    subgraph B["EOD 触发"]
        B1["轮询: 每30秒"] --> B3{"minutes>=900<br/>today>lastEvalDay?"}
        B2["gmsdk tick 回调<br/>15:05-15:30"] --> B3
        B3 -->|是| B4["doEvaluate(today)"]
        B3 -->|否| B1
    end

    subgraph C["EOD 评估"]
        B4 --> C1["去重: evalDay<=lastEvalDay?"]
        C1 -->|是| C0["return"]
        C1 -->|否| C2{"isCompensation<br/>(evalDay<today)?"}
        C2 -->|是| C3["仅更新日期, 不下单"]
        C2 -->|否| C4["evaluateEndOfDay()"]
    end

    subgraph D["下单执行"]
        C4 --> D1["8个子函数执行"]
        D1 --> D2["TradeExecutionEngine"]
    end
```

## 二、启动阶段详细流程

```mermaid
sequenceDiagram
    participant UI as QML UI
    participant SB as StrategyBridge
    participant SM as StrategyManager
    participant PG as PostgreSQL
    participant SE as StrategyEngine
    participant DS as DailyEodScheduler
    participant MDS as MarketDataService

    UI->>SB: start(strategyId)
    SB->>SB: ThreadPoolExecutor::post(async)
    SB->>SM: startStrategy(strategyId)

    SM->>SM: getOrCreateEngine(strategyId)
    SM->>SM: createFactorService()
    SM->>SE: fromDb(strategyId, factorSvc)
    SE->>PG: SELECT metadata_json,parameters FROM strategy
    PG-->>SE: 策略配置 JSON
    SE->>SE: 解析 → StrategyCreationParams<br/>解析 → factorOverlay / ruleGate
    SE->>SE: Builder.build() → new StrategyEngine<br/>  ├─ StrategyService(factorService, ruleService)<br/>  └─ configureExecutionPlan(plan)
    SE-->>SM: unique_ptr&lt;StrategyEngine&gt;

    SM->>SE: engine->start()
    Note over SE: state_ → Running

    SM->>SE: prepareMarketData()
    SE->>PG: SELECT … FROM mkt.daily_bar<br/>WHERE trade_date BETWEEN start AND now
    PG-->>SE: 60天×5290标的 OHLCV + 因子字段
    SE->>SE: factorService_->buildLiveView(rows)
    Note over SE: liveMarketView 就绪

    SM->>SE: setOrderListener(TradeExecutionEngine)
    SM->>SE: startLiveLoop()

    SE->>DS: new DailyEodScheduler(postFn, persistPath)
    SE->>DS: setEodTriggerTime / setEvalCallback
    SE->>DS: start()
    DS->>DS: loadLastEvalDay() ← app_state.json
    DS->>DS: 轮询线程启动 (每30秒)
    DS->>MDS: registerEndOfDayCallback(onEodTrigger)
```

## 三、EOD 触发阶段

```mermaid
flowchart TD
    subgraph "路径A: 轮询"
        A1["schedulePollCheck()"] --> A2{"minutes >= 900?"}
        A2 -->|否| A3["sleep 30秒 → 重试"]
        A3 --> A1
        A2 -->|是| A4{"today > m_lastEvalDay?"}
        A4 -->|否| A5["sleep 60分 → 重试"]
        A5 --> A1
        A4 -->|是| A6["m_post → doEvaluate(today)"]
    end

    subgraph "路径B: gmsdk tick 回调"
        B1["MarketDataService::onTick()"] --> B2{"15:05-15:30<br/>且未评估?"}
        B2 -->|是| B3["fireCallbacksForDay()"]
        B3 --> B4["onEodTrigger(tradingDay)"]
        B4 --> B5{"m_eodRegistered?"}
        B5 -->|是| B6["m_post → doEvaluate(tradingDay)"]
        B5 -->|否| B7["return"]
    end

    A6 --> C["doEvaluate()"]
    B6 --> C
```

## 四、doEvaluate — 评估入口

```cpp
// DailyEodScheduler.cpp:131
void DailyEodScheduler::doEvaluate(const std::string& tradingDay) {
    auto evalDay = std::stoll(tradingDay);

    // ── 1. 去重 ──
    if (evalDay <= m_lastEvalDay.load()) return;  // 已评估

    // ── 2. 补偿判定 ──
    auto today = getCurrentTradingDay();
    bool isCompensation = (evalDay < today);

    // ── 3. 补评: 仅更新日期, 不下单 ──
    if (isCompensation) {
        INTERNAL_INFO_STREAM << "补评: 仅更新日期, 跳过评估";
        m_lastEvalDay.store(evalDay);
        persistLastEvalDay();  // → app_state.json (tmp+rename 原子写)
        return;
    }

    // ── 4. 执行评估 ──
    EodEvaluationStatus status;
    try {
        status = m_evalFn(tradingDay, false);  // → evaluateEndOfDay()
    } catch (const std::exception& e) {
        status = EodEvaluationStatus::Error;
    } catch (...) {
        status = EodEvaluationStatus::Error;
    }

    // ── 5. 持久化 ──
    if (status == Submitted || status == NoSignal) {
        m_lastEvalDay.store(evalDay);
        persistLastEvalDay();
    }
    // 其他状态不持久化, 允许下次重试
}
```

## 五、evaluateEndOfDay — 8 个子函数

```mermaid
flowchart TD
    E["evaluateEndOfDay(tradingDay, false)"] --> F1

    F1["① checkRebalanceDay(tradingDay)"]
    F1 -->|"非调仓日"| R1["return Skipped"]
    F1 -->|"是调仓日"| F2

    F2["② 守卫: backtest? listener?"]
    F2 -->|"是"| R1
    F2 -->|"否"| F3

    F3["③ prepareEodContext(tradingDay) → ctx"]
    F3 -->|"view 为空"| R1
    F3 -->|"成功"| F4

    F4["④ fetchTodayPrices(ctx) → prices"]
    F4 -->|"gmsdk 批量调用"| F5

    F5["⑤ computeMarketBreadth(ctx, prices)"]
    F5 -->|"5290标的 todayClose vs view MA60"| F6

    F6["⑥ 账户/持仓快照"]
    F6 -->|"totalAsset<=0"| R1
    F6 -->|"正常"| F7

    F7["⑦ evaluateEodGates(ctx, breadth)"]
    F7 --> F7a["├─ 宽度≤35% → freeze"]
    F7 --> F7b["├─ ruleGate.allowNewEntriesToday()"]
    F7 --> F7c["└─ 择时 MA20/MA60 → forceLiquidate?"]

    F7c -->|"forceLiquidate"| R2["liquidateAll() → Submitted"]
    F7c -->|"正常"| F8

    F8["⑧ collectEodSignals(ctx, prices, gates, posQtyMap)"]
    F8 --> F8a["├─ 冻结? → 仅评估持仓标的"]
    F8 --> F8b["├─ 涨跌停过滤: 涨停不买, 跌停不卖(策略)"]
    F8 --> F8c["└─ step(mdp) → 因子定池→策略评估→规则过滤"]

    F8 --> F9["⑨ finalizeAndSubmit(ctx, pendingOrders, posQtyMap, prices)"]
    F9 --> F9a["├─ 规则闸门信号审核 (allowSignal)"]
    F9 --> F9b["├─ 规则闸门持仓出场 (positionAction)"]
    F9 --> F9c["│   ├─ 最小持有期检查 (minHoldDays)"]
    F9 --> F9d["│   └─ 风控出场不检查跌停"]
    F9 --> F9e["├─ OrderGenerator::generate()"]
    F9 --> F9f["│   ├─ computeBuyDelta  (OPEN/ADD)"]
    F9 --> F9g["│   ├─ computeSellDelta (REDUCE/CLOSE)"]
    F9 --> F9h["│   └─ compressBuyTotalWeight"]
    F9 --> F9i["└─ onOrders() → TradeExecutionEngine"]

    F9i --> R3["return Submitted/NoSignal/AllRejected"]
```

## 六、关键数据结构

### EodContext — 视图/标的信息
```
struct EodContext {
    view:          IMarketDataView*      // 5290标的×60天矩阵
    symbols:       vector<string>*        // ["000001.SZ","000002.SZ",...]
    dates:         vector<DomainDate>*    // [20260701, 20260702, ...]
    numCols:       int                    // 5290
    rowStride:     int                    // ≥ numCols
    symToCol:      map<string,int>        // "000001"→0
    tradingDayInt: int32_t               // 20260806
    endDateStr:    string                // "2026-08-06"
}
```

### EodPriceData — gmsdk 价格数据
```
struct EodDayBar {
    close:    double    // 当日收盘价(adjust=true)
    volume:   double    // 成交量
    preClose: double    // 前收盘价(涨跌停计算)
}
struct EodPriceData {
    bars:              map<string,EodDayBar>  // "000001.SZ"→{close,vol,preClose}
    breadthAboveMa60:  double                 // 全市场站上MA60比例
}
```

### EodGateResult — 闸门结果
```
struct EodGateResult {
    allowNewEntries:  bool           // 是否允许开新仓
    timing:           TimingResult   // 择时结果(targetExposure,forceLiquidate,...)
}
```

### PendingOrder — 待处理订单
```
struct PendingOrder {
    order:        OrderRequest
    tickPrice:    double       // 市价
    targetWeight: double       // 目标权重
    signalScore:  double       // 信号强度
}
```

## 七、数据流图

```mermaid
flowchart LR
    subgraph S1["启动时"]
        PG["PostgreSQL"] -->|"60天OHLCV"| VIEW["liveMarketView<br/>close[60×5290]<br/>volume[60×5290]<br/>symbolStrings[5290]"]
    end

    subgraph S2["EOD 触发时"]
        VIEW -->|"symbolStrings()"| SYM["5290标的"]
        SYM -->|"逗号拼接"| GMLIST["SHSE.000001,..."]
        GMLIST -->|"history_bars_n<br/>(1次批量调用)"| GM["gmsdk"]
        GM -->|"5290根日线<br/>(close,volume,pre_close)"| PRICES["EodPriceData"]
    end

    subgraph S3["评估时"]
        PRICES -->|"close/vol"| LOOP["collectEodSignals"]
        VIEW -->|"close矩阵"| BREADTH["computeMarketBreadth<br/>todayClose vs MA60"]
        VIEW -->|"close矩阵"| TIMING["evaluateEodGates<br/>CSI300 MA20/MA60"]
        VIEW -->|"symToCol"| RULE["规则闸门<br/>信号审核/持仓出场"]
        ACCT["AccountEngine<br/>账户/持仓"] --> LOOP
        ACCT --> ORDERGEN["OrderGenerator"]
        BREADTH --> GATES["EodGateResult"]
        TIMING --> GATES
        GATES --> LOOP
        LOOP -->|"pendingOrders"| FINAL["finalizeAndSubmit"]
        FINAL -->|"finalOrders"| TE["TradeExecutionEngine"]
    end
```

## 八、各阶段决策点

### 8.1 跳过条件 (evaluateEndOfDay 返回 Skipped)

| # | 条件 | 位置 |
|---|------|------|
| 1 | 非调仓日 | `checkRebalanceDay()` |
| 2 | 回测模式 | L887 |
| 3 | 无订单监听器 | L891 |
| 4 | liveMarketView 为空 | `prepareEodContext()` |
| 5 | account.totalAsset ≤ 0 | L947 |

### 8.2 单标的跳过 (collectEodSignals 内 continue)

| # | 条件 |
|---|------|
| 1 | 冻结且非持仓标的 |
| 2 | 不在 gmsdk 价格表中 |
| 3 | price ≤ 0 |
| 4 | 无效 AStockSymbol |
| 5 | 事件风控封禁 |
| 6 | 涨停且 order=Buy |
| 7 | 跌停且 order=Sell (策略卖) |

### 8.3 持仓出场跳过 (finalizeAndSubmit 内)

| # | 条件 |
|---|------|
| 1 | 持仓量 ≤ 0 或成本价 ≤ 0 |
| 2 | 最小持有期未满 (`countTradingDaysBetween < minHoldDays`) |
| 3 | 规则未触发 Exit/Reduce |

## 九、OrderGenerator 加仓/减仓/清仓决策

```mermaid
flowchart TD
    GEN["generate(rawOrders, posProvider, account, price)"] --> LOOP

    subgraph LOOP["遍历 rawOrders"]
        D1["去重: 同标的+方向"]
        D2["获取 currentQty = posProvider.quantityOf(code)"]
        D3["计算 currentWeight = qtyToWeight(currentQty)"]

        D4{"side == Buy?"}
        D4 -->|是| BUY["computeBuyDelta()"]
        BUY --> B1{"currentWeight < 0.001"}
        B1 -->|是| B2["OPEN: deltaQty = targetQty"]
        B1 -->|否| B3{"targetWeight > currentWeight"}
        B3 -->|是| B4["ADD: deltaQty = targetQty - currentQty"]
        B3 -->|否| B5["丢弃"]

        D4 -->|否| SELL["computeSellDelta()"]
        SELL --> S1{"currentQty <= 0"}
        S1 -->|是| S5["丢弃"]
        S1 -->|否| S2{"targetWeight >= currentWeight"}
        S2 -->|是| S5
        S2 -->|否| S3{"targetWeight > 0"}
        S3 -->|是| S3a{"targetQty < 100"}
        S3a -->|是| S3b["CLOSE: deltaQty = currentQty"]
        S3a -->|否| S3c["REDUCE: deltaQty = currentQty - targetQty"]
        S3 -->|否| S4{"requestedQty > 0<br/>且 < currentQty"}
        S4 -->|是| S4a["REDUCE: deltaQty = requestedQty<br/>(规则出场减半)"]
        S4 -->|否| S4b["CLOSE: deltaQty = currentQty"]

        B2 --> VALIDATE
        B4 --> VALIDATE
        S3b --> VALIDATE
        S3c --> VALIDATE
        S4a --> VALIDATE
        S4b --> VALIDATE

        VALIDATE{"deltaQty >= 100"}
        VALIDATE -->|是| PUSH["buildSignalOrder → result"]
        VALIDATE -->|否| DROP["丢弃"]
    end

    LOOP --> COMPRESS["compressBuyTotalWeight()<br/>总敞口>100% → 等比压缩"]
    COMPRESS --> RETURN["return finalOrders"]
```

## 十、线程模型与同步

| 线程 | 职责 | 关键数据 |
|------|------|----------|
| UI 主线程 | QML 渲染 | — |
| Bridge 工作线程 | StrategyManager::startStrategy | — |
| 策略专用线程 | evaluateEndOfDay, step, doEvaluate | — |
| Poll 线程 | 每30秒检查触发时间 | `m_lastEvalDay` (atomic) |
| gmsdk 回调线程 | onTick → MarketDataService | `m_eodRegistered` (atomic) |

### 同步机制

| 数据 | 保护方式 |
|------|----------|
| `m_loopRunning` | `std::atomic<bool>` |
| `m_lastEvalDay` | `std::atomic<int64_t>` |
| `m_eodRegistered` | `std::atomic<bool>` + acquire/release |
| `m_lastProcessedAt` | `std::atomic<int64_t>` |
| AccountEngine `m_cachedAccount` | `std::shared_mutex` |
| AccountEngine `m_cachedPositions` | `std::shared_mutex` |
| StrategyService `mutex_` | `std::mutex` (所有 public 方法持锁) |

## 十一、所有改动记录

| 日期 | 内容 |
|------|------|
| 08-07 | `evaluateEndOfDay` 拆为 8 个子函数 |
| 08-07 | `OrderGenerator` 拆为 computeBuyDelta / computeSellDelta / compressBuyTotalWeight |
| 08-07 | `liveMarketView()` 支持因子策略 (factorService_->liveView()) |
| 08-07 | 补评不下单 (doEvaluate 拦截) |
| 08-07 | 冻结不 break (blockNewBuys + 仅评估持仓标的) |
| 08-07 | 当日市场宽度替代 T-1 (gmsdk close + view MA60) |
| 08-07 | 批量 gmsdk 取价 (1次调用替代 5290 次) |
| 08-07 | 下单前重取账户快照 |
| 08-07 | `m_eodRegistered` → atomic + acquire/release |
| 08-07 | `m_lastEvalDay` → atomic |
| 08-07 | AccountEngine → shared_mutex |
| 08-07 | 涨停不买 / 跌停不卖(策略) / 风控出场不检查跌停 |
| 08-07 | 最小持有期检查 + 建仓日期记录 |
| 08-07 | Rule Reduce 尊重显式数量 (requestedQty) |
