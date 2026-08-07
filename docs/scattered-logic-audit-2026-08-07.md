# 全项目散装逻辑审计报告

> 日期: 2026-08-07 | 状态: 仅分析，未修改 | 范围: 全项目 4 层

---

## 总览

| 层 | P0 | P1 | P2 | P3 |
|---|----|----|----|-----|
| Engine | 3 | 6 | 7 | 2 |
| Domain | 1 | 5 | 15+ | 6+ |
| Bridge/UI | 5 | 14 | 10 | 0 |
| Infrastructure | 7 | 12 | 18 | 4 |
| **合计** | **16** | **37** | **50+** | **12+** |

---

## P0 — 必须立即修复（安全漏洞 / 数据竞争 / 崩溃风险 / 密码泄露）

### I1. 硬编码数据库密码 `"astock123"`

- **文件**: [NativePgConnectionPool.cpp:40](src/infrastructure/src/NativePgConnectionPool.cpp#L40)
- **问题**: `cfg.password = cfgMgr.get_app_config_string("pg.password", "astock123");` 将明文密码写入版本控制
- **修复**: 改为从环境变量或加密配置文件读取

### I2. 硬编码数据库密码 `"123456a"` + 错误端口

- **文件**: [MarketDataServiceFactory.h:14-18](src/infrastructure/include/MarketDataServiceFactory.h#L14-L18)
- **问题**: 明文密码 + 端口 3306(MySQL) 但连接池是 PostgreSQL(5432)
- **修复**: 移除硬编码，修正端口

### I3. Qt 类型泄露到基础设施层

- **文件**: [StrategyRepository.h](src/infrastructure/include/strategy/StrategyRepository.h), [ResolvedStrategyBehaviorVariant.h](src/infrastructure/include/strategy/ResolvedStrategyBehaviorVariant.h)
- **问题**: 基础设施层声称纯 C++，却大量使用 `QString`, `QVariantMap`, `QDateTime`, `QMutex`, `QMetaType`
- **修复**: 重构为 `std::string` / `std::variant` / 自定义 JSON 类型

### I4. 未检查的 DB INSERT（静默数据丢失）

- **文件**: [BacktestResultRepository.cpp:289,328](src/infrastructure/src/BacktestResultRepository.cpp)
- **问题**: `saveFactorBacktest()` 和 `saveDailySnapshot()` 执行 `m_db.executeQuery()` 后直接 `return true`，不检查返回值
- **修复**: 检查 affected rows 并错误时返回 false + 日志

### I5. GmSessionEngine 的 public 成员变量

- **文件**: [GmSessionEngine.h:149-153](src/engine/include/GmSessionEngine.h#L149-L153)
- **问题**: `m_strategy`, `m_tickMutex`, `m_tickRefCount`, `m_quoteCache` 全部 public，标注 "SessionStrategy needs direct access" — 这正是 `friend` 的使用场景
- **修复**: 改为 private，添加 `friend class SessionStrategy`，必要时加访问器

### I6. 全局裸指针 use-after-free 风险

- **文件**: [GlobalEventBusRegistry.cpp:7-9](src/engine/src/GlobalEventBusRegistry.cpp#L7-L9)
- **问题**: `EventBus* g_engine_event_bus = nullptr;` 全局裸指针，Engine 关闭时若销毁 EventBus，所有持有者悬垂引用
- **修复**: 改为 `std::shared_ptr<EventBus>` 或在 shutdown 时显式置空 + 通知

### I7. m_quoteCache 数据竞争

- **文件**: [GmSessionEngine.cpp:179](src/engine/src/GmSessionEngine.cpp#L179)
- **问题**: `on_tick()` 中写入 `m_quoteCache` 未持有 `m_tickMutex`，而 `fetchQuote()` 通过锁读取 → 并发读写 = UB
- **修复**: `on_tick()` 中写入前加锁

### I8. recentOrders() 无锁返回引用 → 数据竞争

- **文件**: [TradeExecutionEngine.cpp:416-418](src/domain/trading/TradeExecutionEngine.cpp#L416-L418)
- **问题**: `recentOrders()` 返回 `const std::vector<TradeOrder>&` 不加锁，而所有写路径持有锁 → 并发读写数据竞争
- **修复**: 改为返回值拷贝 + 拷贝时加锁，或返回 `std::shared_lock` 保护的视图

### I9. 250 行裸 QVariantMap 组装

- **文件**: [FactorBacktestBridge.cpp:334-584](src/ui/bridge/src/FactorBacktestBridge.cpp#L334-L584)
- **问题**: `processRunResult()` 用 `"key"`, `"title"`, `"value"`, `"format"` 等裸字符串 key 构造 15+ 层嵌套 QVariantMap，拼写错误静默失败
- **修复**: 定义 typed DTO（`CoreMetricItem` / `RatingCheckItem`）并实现 `toMap()` 方法

### I10. 裸 QVariantMap 交易对象构造

- **文件**: [TradingBridges.cpp:92-191](src/ui/bridge/src/TradingBridges.cpp#L92-L191)
- **问题**: `"brokerOrderId"`, `"symbol"`, `"side"`, `"price"`, `"status"` 等 key 在 5+ 回调中重复，C++/QML 两端 key 不一致风险
- **修复**: 定义 key 常量化 header (`OrderFieldKeys.h`) 或 typed DTO

### I11. 1329 行 QML 文件

- **文件**: [BacktestResultPanel.qml](src/app/Qml/components/Backtest/BacktestResultPanel.qml)
- **问题**: 违反 CLAUDE.md 30 行限制，44 倍超标
- **修复**: 拆分为子组件

### I12. 1071 行 QML 文件

- **文件**: [BacktestPage.qml](src/app/Qml/components/FactorWorkbench/Backtest/BacktestPage.qml)
- **问题**: 违反 CLAUDE.md 30 行限制，35 倍超标
- **修复**: 拆分为子组件

### I13. 504 行 QML 文件

- **文件**: [DataSelectionPanel.qml](src/app/Qml/components/DataAnalysis/DataSelectionPanel.qml)
- **问题**: 违反 CLAUDE.md 30 行限制，16 倍超标
- **修复**: 拆分为子组件

### I14. 未检查的 DDL 执行

- **文件**: [BacktestResultRepository.cpp:124-131](src/infrastructure/src/BacktestResultRepository.cpp#L124-L131)
- **问题**: `ensureTables()` 中 `CREATE TABLE` / `CREATE INDEX` 的 `executeQuery` 返回值被忽略，DDL 失败仍返回 `true` 且设置 `m_tablesEnsured = true`
- **修复**: 检查返回值，失败则返回 false

### I15. ConfigManager public 成员

- **文件**: [ConfigManager.hpp:57-59](src/foundation/include/foundation/config/ConfigManager.hpp#L57-L59)
- **问题**: `snapshots_` 和 `change_history_` 声明为 public，外部代码可随意破坏配置历史
- **修复**: 改为 private，添加 const 访问器

### I16. 140 行 syncDailyRange 函数

- **文件**: [PostMarketSyncService.cpp:873-1009](src/infrastructure/src/PostMarketSyncService.cpp#L873-L1009)
- **问题**: 包含 3 个独立 pass（fetch / retry / valuation）的单体函数
- **修复**: 拆分为 `fetchDailyRange`, `retryFailed`, `syncValuation`

---

## P1 — 高优先级（正确性 / 维护性风险）

### E1. 日期格式化逻辑重复 20+ 处

- **涉及文件**: `FactorBacktestOrchestrator.cpp`(12+), `StrategyEngineFacade.cpp`, `RuntimeFactorSvc.cpp`(4), `RuleVariableProvider.cpp`(3), `FactorEngine.cpp`, `BaseFactor.cpp`, `DailyEodScheduler.cpp`, `TradeExecutionEngine.cpp`, `PostMarketSyncService.cpp`(20+)
- **模式**: `int y = date / 10000; int m = (date / 100) % 100; int d = date % 100; snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);`
- **修复**: 提取为 `foundation::utils::formatTradingDay(int yyyymmdd)` 或 `DateKey::toString()`

### E2. toGmSide/toGmType 匿名 namespace 重复

- **文件**: [GmSessionEngine.cpp:324-325](src/engine/src/GmSessionEngine.cpp#L324-L325), [TradeEngine.cpp:16-17](src/engine/src/TradeEngine.cpp#L16-L17)
- **问题**: 两份完全相同的 `int toGmSide(OrderSide)` 和 `int toGmType(OrderType)`
- **修复**: 统一到 GmSessionEngine 作为 public static 方法

### E3. void* 类型擦除跨 4 个类

- **文件**: AccountEngine.h, TradeEngine.h, OrderManager.h, GmSessionEngine.h
- **问题**: 均使用 `void* m_strategy` 持有 gmsdk `::Strategy` 指针，每次调用 `static_cast<::Strategy*>`
- **修复**: 用前向声明 `class Strategy;` + 具体类型指针替代 `void*`

### E4. 交易时段 magic number

- **文件**: [GmSessionEngine.cpp:22-27,131-133,386,399](src/engine/src/GmSessionEngine.cpp)
- **问题**: 涨跌停比例 (10.0/20.0/30.0)、集合竞价时间 (9/15/25/14/57)、gmsdk 状态码 (2/3/5/6/7/8) 全部裸常量
- **修复**: 定义命名常量或枚举

### E5. BacktestScheduler raw new/delete

- **文件**: [BacktestScheduler.cpp:10,14](src/domain/scheduler/src/BacktestScheduler.cpp#L10)
- **问题**: `m_governor = new ResourceGovernor(...)` + `delete m_governor`，无异常安全
- **修复**: 改为 `std::unique_ptr<ResourceGovernor>`

### E6. DataCacheParquet raw delete → 内存泄漏风险

- **文件**: [DataCacheParquet.cpp:667](src/domain/cleaning/src/DataCacheParquet.cpp#L667)
- **问题**: `ArrowWriteToken` = `WriteSession*` typedef，`finishArrowWrite` 手动 `delete` — 调用者忘记则泄漏
- **修复**: 改为 `std::unique_ptr<WriteSession, WriteSessionDeleter>`

### E7. static int 诊断计数器数据竞争

- **涉及文件**: `OrderGenerator.cpp`(2), `MultiFactorStrategy.cpp`(5), `RuntimeFactorSvc.cpp`(2), `RiskManager.cpp`(1)
- **问题**: `static int genDiag = 0; if (genDiag++ < 5) ...` — 非原子 int 跨线程并发递增 = UB
- **修复**: 改为 `std::atomic<int>` 或 `thread_local`

### E8. 千行级单体函数

- **文件**: [FactorBacktestOrchestrator.cpp:61-1130](src/domain/factor/src/FactorBacktestOrchestrator.cpp) — `run()` 1098 行
- **文件**: [StrategyEngineFacade.cpp:1733-2957](src/domain/strategy/src/StrategyEngineFacade.cpp) — `backtest()` 1226 行
- **修复**: 拆分为独立阶段函数

### E9. 佣金/税率重复定义 → 修改不同步风险

- **文件**: [PnlCalculator.h:20-22](src/domain/trading/PnlCalculator.h), [BacktestFillSimulator.h:12-14](src/domain/backtest/include/BacktestFillSimulator.h)
- **问题**: 佣金率 0.0003、印花税 0.001 在两处独立 struct 中定义，修改时可能遗漏
- **修复**: 提取到 `domain::trading::FeeConstants` 或配置

### E10. toOrderStatusValue 两套 switch 并存

- **文件**: [TradeExecutionEngine.cpp:24-36](src/domain/trading/TradeExecutionEngine.cpp) (匿名 namespace), :441-448, :517-530 (两个内联 switch)
- **问题**: 状态码映射定义了 3 次（1 次函数 + 2 次内联 switch），新增状态需更新三处
- **修复**: 统一使用已有的 `toOrderStatusValue()` 函数，删除内联 switch

### E11. 桥接层领域逻辑泄露

多个关键案例：

| 位置 | 泄露的逻辑 | 应归属 |
|------|-----------|--------|
| [TradingBridges.cpp:875](src/ui/bridge/src/TradingBridges.cpp#L875) | VaR 计算 `exposurePct * 1.49` | `RiskEvaluator` |
| [TradingBridges.cpp:570-577](src/ui/bridge/src/TradingBridges.cpp#L570-L577) | 期货/期权/股票价格舍入规则 | `domain::trading` |
| [CandleDataModel.cpp:22-176](src/ui/bridge/src/CandleDataModel.cpp#L22-L176) | SMA/EMA/MACD/KDJ/RSI 完整计算 | 技术指标服务 |

### E12. toConfigNode/toVariantMap 重复

- **文件**: [RiskConfigService.cpp:16-28](src/ui/bridge/src/RiskConfigService.cpp), [TradingConnectionConfigService.cpp:19-33](src/ui/bridge/src/TradingConnectionConfigService.cpp)
- **问题**: `ConfigNode ↔ QVariantMap` 转换逻辑完全相同，两份独立拷贝
- **修复**: 提取到 `ConfigVariantAdapter.h`

### E13. 策略参数 DSL 在桥接层

- **文件**: [StrategyBridge.cpp:975-1022](src/ui/bridge/src/StrategyBridge.cpp#L975-L1022)
- **问题**: 47 行 `buildParamConfigs` 定义每个策略类型的参数 UI 配置（滑条范围/选择项/条件可见性），这是领域知识
- **修复**: 移到 `StrategyDefinitionTypes.h` 领域层

### E14. FactorBacktestBridge 未检查 INSERT (5 处)

- **文件**: [FactorBacktestBridge.cpp:601-683](src/ui/bridge/src/FactorBacktestBridge.cpp)
- **问题**: 5 处 `INSERT INTO alpha.factor_backtest_runs...` 忽略返回值
- **修复**: 检查 affected rows

### E15. Arrow I/O 的 ValueOrDie() 崩溃风险

- **文件**: [DataCleaningServiceRefactored.cpp:194-196](src/ui/bridge/src/DataCleaningServiceRefactored.cpp)
- **问题**: `ReadableFile::Open().ValueOrDie()` — 文件损坏直接 crash 进程
- **修复**: 检查 Status，失败发射错误信号

### E16. DB 连接守卫模式重复 40+ 次

- **涉及文件**: `OrderRecorder.cpp`(4), `EventBridgePoller.cpp`(1), `PostMarketSyncService.cpp`(40+), `StrategyRepository.cpp`(1 via `sdb()`)
- **模式**: `auto db = NativePgConnectionPool::instance().getConnection(); if (!db || !db->isOpen()) return;`
- **修复**: 提取 `ConnectionGuard` 或 `DbSession` RAII 类（已有 `ScopedConnection` 设计但未采用）

### E17. safeStr SQL 转义缺失

- **文件**: [BacktestResultRepository.cpp](src/infrastructure/src/BacktestResultRepository.cpp)
- **问题**: `saveStrategyBacktest` 等函数使用裸字符串拼接 `<< "'" << value << "'"`，不对单引号转义 — SQL 注入
- **对比**: `MarketDataRepository.cpp` 有 `safeStr()` 辅助函数正确转义
- **修复**: 统一使用 `safeStr()` 或参数化查询

### E18. toGmSymbol map 构造重复 6 次

- **文件**: [PostMarketSyncService.cpp](src/infrastructure/src/PostMarketSyncService.cpp) — `syncDaily`, `syncMinute`, `syncDailyRange`, `syncDailyMinute`, `syncValuation`, `syncFinancial`
- **模式**: 各函数内重复 `std::unordered_map<std::string, std::string> gmToSym; ... for (auto& sym : symbols) { gmToSym[toGmSymbol(sym)] = sym; }`
- **修复**: 提取为 `buildGmSymbolMap(const std::vector<std::string>& symbols)`

### E19. 批量 upsert 模式重复 5 次

- **文件**: [PostMarketSyncService.cpp](src/infrastructure/src/PostMarketSyncService.cpp) — `syncDaily`, `syncMinute`, `syncDailyRange`, `syncValuation`, `syncDailyMinute`
- **模式**: `std::vector<std::vector<SqlParam>> batch; auto flush = [&](){ for(auto& p:batch) db->executeUpdate(sql,p); batch.clear(); };`
- **修复**: 提取 `BulkUpserter` 模板类

### E20. 硬编码 schema 名散落 100+ SQL

- **文件**: 所有 repository 和 PostMarketSyncService
- **问题**: `mkt.daily_bar`, `ref.symbol_info`, `live.strategy`, `data.live_order` 等 schema 名散布在各处
- **修复**: 定义 `SchemaNames` namespace 集中管理

### E21. ThreadPoolExecutor 裸指针传递给线程 lambda

- **文件**: [ThreadPoolExecutor.cpp:76](src/infrastructure/src/ThreadPoolExecutor.cpp#L76)
- **问题**: `worker = worker.get()` 裸指针在 shutdown 时可能悬垂
- **修复**: 改用 `std::shared_ptr<Worker>` 或确保 `workers_` 在 join 前不清理

---

## P2 — 中优先级（代码质量 / 架构卫生）

### 重复代码

- **F2.1**: Spearman 秩相关在 `FactorBacktestOrchestrator.cpp`(2 次) 和 `StrategyEngineFacade.cpp`(2 次) 独立实现
- **F2.2**: `generateClOrdId()` 在 `OrderBuilder.cpp` 和 `StrategyEngineFacade.cpp` 各有一份
- **F2.3**: 日期分解 `int y=date/10000, m=(date%10000)/100, d=date%100` 与 `(date/100)%100` 的月份提取不一致（潜在 bug）
- **F2.4**: 仓位→QVariantMap 组装在 `TradingBridges.cpp` 两处重复（PositionAccountBridge / RiskControlBridge）
- **F2.5**: JsonFacade row→QVariantMap 在 `DataCacheAdapter.cpp` 两处近似重复
- **F2.6**: Arrow→LightRow 转换在 `DataCleaningServiceRefactored.cpp` 两处近似重复
- **F2.7**: FNV-1a 64 哈希在 `RuleConditionEvaluator.cpp` 匿名 namespace 内两次实现

### 大函数 / 大类

- **F2.8**: `EngineImpl::initialize()` 111 行（4 个独立职责：clock/event bus/global register/trigger bridge）
- **F2.9**: `EngineImpl::event_loop()` 76 行
- **F2.10**: `TradeExecutionEngine::submitOrder()` 157 行
- **F2.11**: `StrategyEngineFacade::finalizeAndSubmit()` 192 行
- **F2.12**: `StrategyEngineFacade::evaluateEodGates()` 70 行
- **F2.13**: `BacktestResultRepository::saveStrategyBacktest()` INSERT 语句 44 行
- **F2.14**: `MarketDataRepository` 960 行，35 个近似方法
- **F2.15**: `PostMarketSyncService::syncAll()` 85 行
- **F2.16**: `PostMarketSyncService::forceSyncToday()` 103 行
- **F2.17**: `CleaningEngine::clean()` 136 行

### 字符串路由

- **F2.18**: `RuleConditionEvaluator::compileConditionImpl()` — JSON op 字符串 (`"all"`, `"any"`, `"lt"`, `"gt"` 等) 手写 if-else 链
- **F2.19**: `RuleConditionEvaluator::parseAction()` — `"block"` / `"candidate_entry"` 等字符串比较
- **F2.20**: `StrategyEngineFacade::fromDb()` — `factorCombineMode` 字符串 (`"intersection"` / `"union"` / `"quota"`) 分发
- **F2.21**: `RiskEvaluator::parseDirection` — `"BUY"/"Buy"/"buy"` 大小写多重比较
- **F2.22**: EventBus 事件名字符串 — `"trading.order.updated"` / `"trading.market.tick"` 等在发布/订阅端散落，无编译检查

### magic number / 硬编码值

- **F2.23**: 看板手数 `100` 在两处裸写（`TradeExecutionEngine.cpp` vs `OrderGenerator.cpp` 的 `kMinLot`）
- **F2.24**: 涨跌停阈值 `1.098`/`0.902` 裸常量（`StrategyEngineFacade.cpp:913-921`）
- **F2.25**: `maxRecentOrders = 50` 不可配置
- **F2.26**: 重试次数不一致 — `PostMarketSyncService` 中 retry=2 vs retry=3，退避策略也不同
- **F2.27**: 90% 覆盖率阈值、5% 错误容忍度、60 天分块大小 等阈值散落
- **F2.28**: QML 中 `BaseQuantCard.qml` 780 行，`getCategoryIcon()` 34 行 switch-case

### 封装缺失

- **F2.29**: `StoredStrategyBacktest` 40+ public 字段，未分组
- **F2.30**: `DataSetInfo` public struct 混合数据和 `toJson`/`fromJson` 序列化逻辑
- **F2.31**: `StrategyEngine` 的 Builder 直接访问 private 成员 `m_minHoldDays` / `m_factorSignalProcessor`
- **F2.32**: `IMarketDataView::mutableFieldData()` 返回裸指针破坏 const 正确性

### 死代码 / 未用代码

- **F2.33**: `EventBusStats` 结构体定义但方法从未调用
- **F2.34**: `process_batch_events()` 无调用点
- **F2.35**: `ScopedConnection` RAII 类在 `StrategyRepository.h` 中定义但 `StrategyRepository.cpp` 用 `sdb()` 自由函数替代
- **F2.36**: `Event::type_to_string()` 与 `Event.h` 中的 `event_type_to_string()` 重复且 `Event.cpp` 版本过时（缺少 TRADING/BACKTEST/RISK）

---

## P3 — 低优先级（改进建议）

- **P3.1**: `GmQuote::isLimitUp/Down` 的 epsilon `0.05` 不对称（上限 `-0.05` 下限 `+0.05`）
- **P3.2**: `Engine.h` 的 `event_type_to_string` 自由函数应移到 `EventFormat.hpp`
- **P3.3**: `1e-12` epsilon 在 4 处独立定义 — `TradingTypes.cpp`, `HighFreqFactor.cpp`, `MultiFactorStrategy.cpp`, `TechnicalFactor.cpp`
- **P3.4**: `BacktestResultRepository.h` 包含未使用的 Qt 头文件
- **P3.5**: `NativePgDatabase::close()` 和 `~Impl()` 双重 `PQfinish` 冗余守卫
- **P3.6**: `TradeExecutionEngine.cpp:714` — `defType == "Market"` 字符串常量应改 enum
- **P3.7**: `ExtensionSlot` key/value public — 可改为 constructor-required 一次性设置
- **P3.8**: Bridge 层硬编码中文字符串 — 不便国际化
- **P3.9**: `MarketDataRepository` 35 个方法共享 "构建 SELECT → exec → 遍历 → push_back → return vector" 模板 — 可提取 `executeAndMap<T>`

---

## 建议实施路线图

```
Phase A: P0 安全修复 (预计 2-3 天)
├── A1: 移除硬编码 DB 密码 → 环境变量 (I1, I2)
├── A2: recentOrders() 加读锁 (I8)
├── A3: m_quoteCache 写入加锁 (I7)
├── A4: BacktestResultRepository SQL 返回值检查 (I4, I14)
├── A5: 全局 EventBus 裸指针改 shared_ptr + 注销 (I6)
└── A6: ConfigManager snapshots_/change_history_ → private (I15)

Phase B: P0 封装/结构 (预计 3-5 天)
├── B1: GmSessionEngine public 成员 → private + friend (I5)
├── B2: FactorBacktestBridge QVariantMap → typed DTO (I9)
├── B3: TradingBridges 订单 map → typed struct + key 常量化 (I10)
├── B4: Infrastructure Qt 类型解耦 (I3)
├── B5: QML 大文件拆分 BacktestResultPanel/BacktestPage/DataSelectionPanel (I11-I13)
└── B6: syncDailyRange 拆分 (I16)

Phase C: P1 重复消除 (预计 5-7 天)
├── C1: 日期格式化统一 → foundation::utils 或 DateUtils (E1)
├── C2: 提取 DB 连接守卫 RAII → ConnectionGuard/DbSession (E16)
├── C3: toGmSymbol map 构造统一 (E18)
├── C4: 批量 upsert → BulkUpserter 模板 (E19)
├── C5: fromGm/toGm/状态映射统一 (E2, E10)
├── C6: Schema 名常量化 (E20)
├── C7: Bridge 领域逻辑回迁 (E11, E13)
├── C8: void* 改前向声明 (E3)
├── C9: raw new/delete → unique_ptr (E5, E6)
├── C10: static int 诊断 → atomic (E7)
├── C11: 千行函数拆分 (E8)
├── C12: 佣金/税率统一 (E9)
├── C13: toConfigNode 提取 (E12)
├── C14: FactorBacktestBridge INSERT 检查 (E14)
├── C15: Arrow ValueOrDie → 错误处理 (E15)
├── C16: safeStr SQL 转义补全 (E17)
└── C17: ThreadPoolExecutor 裸指针 (E21)

Phase D: P2/P3 技术债 (择机)
├── D1: 字符串路由 → enum map
├── D2: EventBus 事件名常量化 → constexpr string_view
├── D3: 死代码清理 (EventBusStats / process_batch_events / ScopedConnection / type_to_string)
├── D4: QML BaseQuantCard 拆子组件
├── D5: MarketDataRepository 模板化
├── D6: StoredStrategyBacktest 字段分组
├── D7: 硬编码配置值可配置化
└── D8: 中文显示字符串外部化
```

---

## 统计

| 分类 | 数量 |
|------|------|
| 安全问题 (密码泄露 + SQL注入) | 4 |
| 数据竞争 / 线程安全 | 6 |
| 重复代码块 | 25+ |
| 领域逻辑越界 | 8 |
| 大函数/大类 (>100行) | 12+ |
| 封装缺失 (public成员) | 8 |
| 字符串路由 (应改enum) | 7 |
| magic number (非命名常量) | 20+ |
| 裸指针所有权问题 | 4 |
| 未检查返回值 | 10+ |
| 死代码 | 5 |
| Qt依赖泄露 | 4 |
| QML违规 (>30行) | 4 |
