---
name: jujin-market-data-event-mismatch
description: 掘金行情无法到达策略引擎的根因：EventBus 事件类型不匹配
metadata:
  type: project
---

## 根因

**`GmStrategySession` 发布的事件类型** 与 **`JujinMarketConnector` 订阅的事件类型** 不一致：

| 层 | 行情事件类型 | 代码位置 |
|---|---|---|
| GmStrategySession::on_tick() 发布 | `"trading.market.tick"` | [GmStrategySession.cpp:1317](src/ui/bridge/src/GmStrategySession.cpp#L1317) |
| GmStrategySession::on_bar() 发布 | `"trading.market.bar"` | [GmStrategySession.cpp:1349](src/ui/bridge/src/GmStrategySession.cpp#L1349) |
| JujinMarketConnector 订阅 | `"market.tick"` | [JujinMarketConnector.cpp:334](src/app/src/JujinMarketConnector.cpp#L334) |
| JujinMarketConnector 订阅 | `"market.bar"` | [JujinMarketConnector.cpp:347](src/app/src/JujinMarketConnector.cpp#L347) |

C++ SDK 路径发布的 `"trading.market.tick"` 永远无法到达订阅了 `"market.tick"` 的 JujinMarketConnector。

Python 数据源 (`juejin_data_source.py:334`) 发布的是 `"market.tick"`，所以只有 Python 路径能正常工作。

## 修复方向

二选一：
1. **JujinMarketConnector 增加订阅** `"trading.market.tick"` 和 `"trading.market.bar"` — 推荐，不影响其他订阅者
2. **GmStrategySession 改用** `"market.tick"` / `"market.bar"` — 可能影响其他依赖 `"trading.market.*"` 的订阅者

## 验证方法

修复后，启动程序 → 启动策略 → 查看控制台是否有：
```
[JMC] tick #100 sym=...
[RFS] copySnapshots: day=... syms=... factorIds=...
```

## 附录：事件类型定义

```cpp
// EventFormat.hpp
constexpr auto MARKET_TICK = "market.tick";           // EventTypeRegistry: 2006
constexpr auto MARKET_BAR = "market.bar";             // EventTypeRegistry: 2007
constexpr auto TRADING_MARKET_TICK = "trading.market.tick";  // EventTypeRegistry: 2101
constexpr auto TRADING_MARKET_BAR = "trading.market.bar";    // EventTypeRegistry: 2102
```

[[live-trading-architecture]]
