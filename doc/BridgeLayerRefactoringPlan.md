# 桥接层重构计划 - 纯QML桥接、零业务逻辑

## 1. 目标原则

| 原则 | 说明 |
|------|------|
| 桥接层只做类型转换 | QVariant <-> C++ struct 的映射和信号转发 |
| 桥接层不写任何if/switch业务判断 | 所有条件判断、阈值比较、状态转换下沉到域层 |
| 桥接层禁止直接访问数据库 | 禁止database->executeQuery()，禁止Repository直接持有 |
| 桥接层禁止创建域对象 | 禁止new FactorEngine()等工厂行为 |
| 域层不允许Qt代码 | 禁止#include <QString>, <QVariantMap>, <QJsonDocument> |
| 禁止字符串比较做业务判断 | if (side == "BUY") 必须用enum class |
| 禁止QMap/QHash做key业务匹配 | 封装为强类型结构体或domain层专用索引 |

## 2. 架构分层

```
QML UI Layer
Bridge Layer ---------- QVariant <-> C++ struct + 信号转发
Application Facade ---- 编排domain服务(纯C++无Qt, 新建)
Domain Layer ---------- 实体/服务/仓储接口/因子/策略/风控
Infrastructure -------- Repository实现/清洗引擎/EventBus/券商
```

## 3. 逐文件重构方案

### 3.1 TradeExecutionService.cpp (2921行 -> ~200行)

新建域层:
- src/domain/trading/OrderSubmissionPipeline.h/.cpp
- src/domain/trading/ExecutionSchedulingEngine.h/.cpp
- src/domain/trading/OrderConflictDetector.h/.cpp
- src/domain/trading/ExecutionCheckpointManager.h/.cpp

新建域层risk:
- src/domain/risk/RiskApprovalGateway.h/.cpp

新建基础设施层:
- src/infrastructure/event/OrderEventSubscriber.h/.cpp
- src/infrastructure/broker/BrokerOrderGateway.h/.cpp

新建外观层:
- src/app/facade/OrderSubmissionFacade.h/.cpp

桥接层保留: QVariantMap -> OrderSubmissionRequest -> facade调用 -> OrderSubmissionResult -> QVariantMap + 信号转发

### 3.2 DataService.cpp (2848行 -> ~150行)

新建基础设施层:
- src/infrastructure/database/MarketDataRepository.h/.cpp
- src/infrastructure/cleaning/CleaningEngineFactory.h/.cpp

新建域层data:
- src/domain/data/ConstituentMetadataEnricher.h/.cpp

新建外观层:
- src/app/facade/DataQueryFacade.h/.cpp

桥接层保留: QVariantMap -> DataQueryRequest -> facade::queryMarketData() -> QVariantList转换

### 3.3 MarketDataService.cpp (1417行 -> ~250行)

新建基础设施层:
- src/infrastructure/database/MarketSnapshotRepository.h/.cpp
- src/infrastructure/event/MarketDataEventSubscriber.h/.cpp
- src/infrastructure/config/DefaultMarketDataSeeds.h/.cpp

新建域层market:
- src/domain/market/SymbolNormalizer.h/.cpp
- src/domain/market/SnapshotHydrator.h/.cpp
- src/domain/market/PriceColorCalculator.h/.cpp (PriceColor{Green,Red,Blue} enum)

新建外观层:
- src/app/facade/MarketDataFacade.h/.cpp

桥接层保留: QVariant <-> struct 转换 + 信号转发

### 3.4 StrategyBridge.cpp (704行 -> ~200行)

新建域层strategy:
- src/domain/strategy/StrategyLifecycleManager.h/.cpp
- src/domain/strategy/StrategyFactory.h/.cpp (统一管理 Symbol/因子名解析器 + RuntimeFactorSvc创建)
- src/domain/strategy/OrderListenerImpl.h/.cpp (从bridge移出，输出OrderRequest结构体)

新建外观层:
- src/app/facade/StrategyManagementFacade.h/.cpp (CRUD编排+参数校验)

桥接层保留: QVariant <-> struct 转换 + CRUD信号转发

### 3.5 FactorBacktestBridge.cpp (681行 -> ~200行)

新建域层factor:
- src/domain/factor/FactorRatingEngine.h/.cpp (computeRating, computeSummary, buildRatingChecks, 阈值常量在域层)
- src/domain/factor/FactorModeInference.h/.cpp (inferMode -> FactorMode enum)
- src/domain/factor/BacktestOrchestratorFactory.h/.cpp (DI装配)
- src/domain/factor/FactorSupportEvaluator.h/.cpp

新建外观层:
- src/app/facade/FactorBacktestFacade.h/.cpp

桥接层保留: QVariant <-> struct 转换 + 进度信号转发

### 3.6 FactorService.cpp (653行 -> ~150行)

新建域层factor:
- src/domain/factor/FactorTypeRegistry.h/.cpp (displayName, typeId, fromIndex -> FactorType, 不用switch)
- src/domain/factor/FactorDataViewGenerator.h/.cpp
- src/domain/factor/FactorCrudHandler.h/.cpp

新建基础设施层:
- src/infrastructure/factor/FactorBackendInitializer.h/.cpp

桥接层保留: QVariant <-> struct 转换 + 信号转发

### 3.7 StrategyBacktestBridge.cpp (258行 -> ~100行)

并入StrategyFactory统一管理Symbol resolver和FactorName resolver

桥接层保留: QVariant -> BacktestRequest struct转换 + 线程调度 + 信号转发

### 3.8 RiskControlBridge.cpp (114行 -> ~40行)

新建域层risk:
- src/domain/risk/LiveRiskEvaluator.h/.cpp (computeMetrics含峰值资产追踪)
- src/domain/risk/RiskConfigResolver.h/.cpp

桥接层保留: 订阅信号 -> 调用evaluator->computeMetrics() -> 转发结果到QML

### 3.9 TradeExecutionBridge.cpp (212行 -> ~100行)

新建域层trading:
- src/domain/trading/OrderMapper.h/.cpp (fromVariantMap/toVariantMap)
- src/domain/trading/RiskInputFactory.h/.cpp

桥接层保留: 域层variant <-> QVariant的最终转换

## 4. 禁止项对照

| 禁止 | 替代 |
|------|------|
| if(status=="REJECTED") | enum class OrderStatus |
| side=="BUY"字符串映射 | OrderSide::to_string()/from_string() |
| QMap<QString,QVariant>索引 | unordered_map<DomainId,Entity> |
| database->executeQuery() | Repository接口 |
| new域对象 | DI容器/Factory |
| #include<QJsonDocument> | nlohmann::json或std::string |

## 5. 强类型要求

```cpp
// src/domain/trading/TradingTypes.h
enum class OrderSide : uint8_t { Buy, Sell };
enum class OrderStatus : uint8_t { Requested, Pending, Submitted, Filled, Rejected, Cancelled };
enum class ExecutionDecision : uint8_t { Allow, Block, Warn, Pause };
struct OrderSubmissionRequest { Symbol symbol; OrderSide side; Price price; Quantity quantity; StrategyId strategyId; };
struct OrderSubmissionResult { bool accepted; ExecutionDecision decision; std::string message; vector<RuleHit> ruleHits; };
```

## 6. 实施6个阶段

### 阶段1: 基础设施层准备(8步)
1.1 SymbolNormalizer
1.2 补充TradingTypes.h强类型枚举
1.3 MarketDataRepository
1.4 MarketSnapshotRepository
1.5 CleaningEngineFactory
1.6 DefaultMarketDataSeeds
1.7 OrderEventSubscriber
1.8 MarketDataEventSubscriber

### 阶段2: 域层核心逻辑(20步)
2.1 OrderMapper + RiskInputFactory
2.2 OrderConflictDetector
2.3 ExecutionCheckpointManager
2.4 ExecutionSchedulingEngine
2.5 OrderSubmissionPipeline
2.6 RiskApprovalGateway
2.7 LiveRiskEvaluator + RiskConfigResolver
2.8 FactorRatingEngine
2.9 FactorModeInference
2.10 FactorTypeRegistry
2.11 FactorCrudHandler
2.12 FactorDataViewGenerator
2.13 FactorSupportEvaluator
2.14 BacktestOrchestratorFactory
2.15 StrategyFactory
2.16 StrategyLifecycleManager
2.17 OrderListenerImpl
2.18 SnapshotHydrator
2.19 PriceColorCalculator
2.20 ConstituentMetadataEnricher

### 阶段3: 基础设施事件券商因子(5步)
3.1 BrokerOrderGateway
3.2 FactorBackendInitializer
3.3 EventDtoConverter
3.4 完善OrderEventSubscriber集成
3.5 完善MarketDataEventSubscriber集成

### 阶段4: Facade外观层(5步)
4.1 OrderSubmissionFacade
4.2 DataQueryFacade
4.3 MarketDataFacade
4.4 StrategyManagementFacade
4.5 FactorBacktestFacade

### 阶段5: 桥接层精简(10步)
5.1 BridgeTypeConverters.h模板
5.2 精简TradeExecutionService.cpp
5.3 精简DataService.cpp
5.4 精简MarketDataService.cpp
5.5 精简StrategyBridge.cpp
5.6 精简FactorBacktestBridge.cpp
5.7 精简FactorService.cpp
5.8 精简StrategyBacktestBridge.cpp
5.9 精简RiskControlBridge.cpp
5.10 精简TradeExecutionBridge.cpp

### 阶段6: 验证清理(6步)
6.1 全量编译验证
6.2 运行测试套件
6.3 审查桥接层无Qt字符串比较
6.4 审查桥接层无QMap key业务匹配
6.5 审查域层无#include <Q*>
6.6 删除已迁移的bridge冗余代码

## 7. 总计

- 新建域层模块: 20个
- 新建基础设施层模块: 10个
- 新建外观层模块: 5个
- 新建桥接层模板: 1个
- 待精简桥接文件: 10个
- 总计约36个新模块

## 8. 验收标准

- 桥接层.cpp不包含database->executeQuery()
- 桥接层不直接创建domain::或infrastructure::对象
- 桥接层不包含if(value=="xxx")字符串业务判断
- 桥接层不使用QMap<QString,...>做业务索引
- 域层.h/.cpp不包含#include<QString>,<QVariantMap>,<QJsonDocument>
- 桥接层平均行数<250行/文件
