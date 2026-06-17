# AStockQuantEngine — 量化交易系统

## 必须遵守的规则

- **所有提示和注释必须用中文**
- **禁止 `git revert`、`git reset --hard`、`git checkout --` 回滚代码。编译错误必须直接修改源文件修复**
- 不要写模拟/占位代码，除非用户明确要求
- 修改后要验证编译是否通过

## 项目架构

```
src/
├── app/               # 应用层 (QML 页面、系统入口)
│   ├── Qml/           # QML UI (策略库、因子工作台、回测页面)
│   ├── system/        # TradingSystem 启动入口
│   └── adapters/      # 外部适配器 (JujinBrokerGateway)
├── ui/bridge/         # 桥接层 (QML ↔ C++)
│   ├── src/           # StrategyBacktestBridge, FactorBacktestBridge, StrategyBridge...
│   └── include/       # 桥接头文件
├── domain/
│   ├── strategy/      # 策略引擎 (StrategyEngine, StrategyManager, RuntimeFactorSvc)
│   ├── factor/        # 因子计算 (FactorEngine, BacktestDataService, CachedMarketDataView)
│   ├── strategies/    # 策略定义 (MultiFactorSelectionStrategy, TechnicalFactor...)
│   ├── backtest/      # 回测基础设施 (BacktestFillSimulator)
│   ├── trading/       # 交易接口 (IBrokerGateway, PositionAccountEngine)
│   └── cleaning/      # 数据清洗
├── infrastructure/    # 数据库、持久化、Repository
└── foundation/        # 基础库 (线程池、JSON、日志)

tools/                 # Python 工具脚本 (日更流水线、交易日历)
```

## 构建

```bash
# 完整构建
cmake --build build --config Debug --target ALL_BUILD -j 12

# 只构建 bridge 库（已授权）
cmake --build build --config Debug --target bridge
```

## 策略回测数据流

```
QML 点击开始回测
  → StrategyBacktestBridge::runBacktest()        [桥接层: 参数转换 + 线程分发]
    → StrategyManager::createEngine()            [从 DB 加载策略参数]
    → DataServiceCache::getDataSetById()         [加载缓存数据集]
    → BacktestDataService::storeRawJson()        [存储原始 JSON]
    → setBinCachePath()                          [设置 .bin 缓存路径]
    → StrategyEngine::backtest()                 [逐日回测循环]
      → buildViewForFields()                     [构建 MarketView，优先 .bin]
      → setDataService()                         [注入给 RuntimeFactorSvc]
      → stepBatch(MarketDataPoint)               [逐日调用策略管线]
        → StrategyService::onMarketDataBatch()
          → RuntimeFactorSvc::updateBatch()      [更新最新股票列表]
          → evaluateAndCheckRulesBatch()         [策略评估 + 规则检查]
            → copySnapshots() → getValues()      [因子计算]
            → strategy->evaluate()               [策略信号生成]
            → ruleEvaluationService              [规则过滤]
      → RiskEvaluator + FillSimulator            [风控 + 成交模拟]
      → 指标计算                                 [Sharpe/回撤等]
```

## 因子回测数据流 (对比)

```
FactorBacktestBridge::startBacktest()
  → m_backtestDataSvc->storeRawJson()
  → m_backtestDataSvc->setBinCachePath()
  → m_backtestDataSvc->buildViewForFields(allFields)  [显式构建，编排器需要 instrumentCount]
  → m_orchestrator->run()
    → FactorEngine::compute()
      → buildViewForFields(neededFields)              [增量加载因子字段]
      → computeOneDay() × N                           [逐日计算]
```

## 关键约定

### 两种回测路径的差异
- **StrategyBacktestBridge**: 每次新建 BacktestDataService；buildViewForFields 由 engine->backtest() 内部调用
- **FactorBacktestBridge**: 复用 m_backtestDataSvc 实例；显式调 buildViewForFields 获取 instrumentCount；FactorEngine::compute 内部再次调用

### .bin 缓存格式
- 路径: `{persistentDir}/dataset_{id}_data.bin`
- 魔数 `0x42564453` v2: OHLCV 列 + 额外字段矩阵 + symbolStrings
- 写入: `CachedMarketDataView::saveToBinary()`
- 读取: `CachedMarketDataView::fromBinary()`
- BacktestDataService::buildViewForFields 优先从 .bin 加载，失败则回退 JSON 解析

### InstrumentId
- `isValid()` 返回 `value > 0`，ID=0 视为无效
- CachedMarketDataView::fromJson 中 ID 从 0 开始分配
- onMarketDataBatch 遇到无效 MarketDataPoint 会**过滤而非拒绝**整批

### 因子计算
- 所有因子统一使用 `getSeries(symbol, startDate, endDate, field)` 逐标取数据
- `getSeries(symbol, anchorDate, window, field)` 是便捷重载（内部换算 startDate）
- 不再使用 `getBatchTimeSeries`（已删除）

## 已知曾踩过的坑

1. **`m_isRunning` 改了但没 emit**: QML 的 `onIsRunningChanged` 收不到 → 进度条不显示
2. **UI 卡顿**: DB 查询和 JSON 解析必须在线程池中执行，不能在 QML 主线程
3. **RuntimeFactorSvc 未注入 dataSvc**: `backtest()` 必须调 `rfs->setDataService(dataSvc)`
4. **spec.factorWeights 为空**: `fromDb()` 必须从 params.factorIds 等权填充
5. **onMarketDataBatch 遇 ID=0 整批拒绝**: 应过滤单个无效条目而非拒绝整批
6. **getBatchTimeSeries 返回值 key 反了**: `result[sym][field]` vs `result[field][sym]`
7. **InstrumentId(0) 无效**: 导致每天第一个标的被跳过
8. **不要改 CachedMarketDataView 的 ID 起始值和 .bin 版本号**: 兼容性问题大，修 onMarketDataBatch 过滤就够了
