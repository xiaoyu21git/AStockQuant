---
name: live-trading-architecture
description: 策略实盘交易订单系统完整架构（行情→策略引擎→订单执行）
note: 文中 MySQL 引用已于 2026-07 迁移到 PostgreSQL
metadata:
  type: project
---

## 实盘交易系统架构全景

### 两套 SDK 连接

系统存在**两套独立的掘金 SDK 连接**，各用一个 `Strategy` 实例和 `run()` 事件循环：

| 连接 | 类 | 用途 | 线程 |
|---|---|---|---|
| 行情+交易命令 | `GmStrategySession` (extends `Strategy`) | 行情订阅、交易命令路由、订单回执 | 自己的 `runtime_thread_` |
| 下单执行 | `JujinSession` (wraps `Strategy`) | `JujinBrokerGateway` 的下单/查账/查持仓 | `std::async` 线程 |

### 行情数据流（当前存在事件类型不匹配）

```
SDK tick 到达
  → GmStrategySession::on_tick()           [runtime_thread_]
    → 发布 EventBus 事件: "trading.market.tick"
      → JujinMarketConnector 订阅了 "market.tick"  ← 不匹配！收不到！
```

### 策略引擎实盘处理链路

```
QML 点击"启动策略"
  → StrategyBridge::start()                [主线程]
    → m_startupPool->post(lambda)           [线程池]
      → StrategyManager::createEngine()
      → engine->start()                    (StrategyService 状态→Running)
      → 加载历史 MarketView (MySQL, ~60交易日)
      → engine->startLiveLoop()            (启动 drainQueue 专用线程)
        → QMetaObject::invokeMethod 回主线程更新UI状态
```

### Tick→信号→订单 处理链

```
行情 tick 入队
  → StrategyEngine::enqueueMarketData(mdp)  [任意线程]
    → push to m_mdpQueue, notify m_queueCv
      → drainQueue() 唤醒                  [引擎专用线程]
        → step(mdp)
          → StrategyService::onMarketDataPoint(mdp)  [持有 mutex_]
            → RuntimeFactorSvc::updateIncremental()   (锁 m_stateMutex)
            → evaluateAndCheckRulesLowLatency()
              → copySnapshots() → getValues() → computeSingleDate() → factor.calculate()
              → MultiFactorSelectionStrategy::evaluate()  (Z-score 复合评分)
              → ruleEvaluationService_.evaluate()  (规则过滤)
              → buildOrderRequest() → push to pendingOrderBuffer_
        → collectOrders() → copyPendingOrders()
        → m_orderListener->onOrders(orders)
          → StrategyOrderForwarder::onOrders()
            → TradingSystem::submitOrder(order)
              → RiskInput → RiskEvaluator
              → TradeExecutionEngine::submitOrder()
                → JujinBrokerGateway::submitOrder()
                  → JujinSession::place_order() → SDK place_order()
```

### 线程模型

| 线程 | 运行内容 |
|---|---|
| Qt 主线程 | QML UI、StrategyBridge QObject、QTimer 定时查账 |
| StrategyBridgeStartup (1-4线程) | 引擎创建、历史数据加载 |
| StrategyEngineLiveLoop (每引擎1线程) | drainQueue() 无限循环：等 tick → step → 发单 |
| SDK runtime_thread_ | GmStrategySession: `strategy_->run()` 事件循环、`on_tick`/`on_bar` 回调 |
| SDK async 线程 | JujinSession: `m_strategy->run()` 事件循环 |
| Market subscription 线程 | JujinMarketConnector: processSubscriptionRequests() |
| 订单回执对账 | GmStrategySession 内部 reconciliation loop |

### 关键互斥锁

| 锁 | 保护范围 | 持有时机 |
|---|---|---|
| StrategyService::mutex_ | 策略条目、缓冲区、状态 | onMarketDataPoint() 全程持有 |
| StrategyManager::m_mutex | m_engines map | pushMarketData() 遍历引擎时持有 |
| StrategyEngine::m_queueMutex | tick 队列 | enqueue/pop 时短暂持有 |
| RuntimeFactorSvc::m_stateMutex | 最新交易日、标的列表、因子ID | updateIncremental/copySnapshots |
| JujinSession::m_mutex | SDK m_strategy 调用 | 所有网关操作串行化 |

### 已知问题

1. **行情事件类型不匹配**：GmStrategySession 发布 `"trading.market.tick"`，JujinMarketConnector 订阅 `"market.tick"` — C++ 路径收不到行情
2. **委托价格硬编码 1.0**：`StrategyOrderForwarder` 不传递真实市价
3. **每 tick 全量重算因子**：`computeSingleDate()` 无增量更新，5000+ 标的时极慢
4. **成交回报无回调**：`JujinBrokerGateway` 的 setTradeCallback/setErrorCallback 是空实现
5. **TradingSystem::submitOrder 立即模拟成交**：不等券商真实成交回报就更新持仓
6. **双 SDK 连接可能冲突**：同一 account 创建两个 Strategy 实例各自 run()

## 如何应用

- 实盘行情调试：检查 EventBus 事件类型是否匹配（"market.tick" vs "trading.market.tick"）
- 策略开发：理解 drainQueue 是每引擎独立的专用线程，step() 是串行阻塞的
- 性能优化：因子计算瓶颈在每 tick 的 `computeSingleDate()` 全量重算
- 线程安全：所有策略引擎状态修改必须通过 Mutex 或原子操作

[[jujin-market-data-event-mismatch]]
