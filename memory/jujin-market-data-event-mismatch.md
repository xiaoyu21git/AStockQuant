---
name: jujin-market-data-event-mismatch
description: 掘金行情无法到达策略引擎的根因：EventBus 事件类型不匹配
metadata:
  type: project
  status: resolved
---

> **✅ 已修复 (2026-07-02)**: `af74f14` 统一行情桥接层直接订阅 `"trading.market.tick"` (MarketDataBridge.cpp:43)，不再经 JujinMarketConnector 旧路径。`GmSessionEngine::on_tick` 发布的 `"trading.market.tick"` 现在直接到达桥接层。保留此内存作为根因分析参考。

## 根因（历史）

**`GmSessionEngine` 发布的事件类型** 与 **`JujinMarketConnector` 订阅的事件类型** 不一致：

| 层 | 行情事件类型 | 代码位置 |
|---|---|---|
| GmSessionEngine::on_tick() 发布 | `"trading.market.tick"` | [GmSessionEngine.cpp:149](src/engine/src/GmSessionEngine.cpp#L149) |
| JujinMarketConnector 订阅(旧) | `"market.tick"` | [JujinMarketConnector.cpp:334](src/app/src/JujinMarketConnector.cpp#L334) |

## 修复方案（已实施）

统一行情桥接层 (`MarketDataBridge.cpp:43`) 直接订阅 `"trading.market.tick"`，在 `processTick()` 中同时更新行情快照和 K 线蜡烛，不再依赖 JujinMarketConnector 中转。

[[live-trading-architecture]]
[[version-management-plan]]
