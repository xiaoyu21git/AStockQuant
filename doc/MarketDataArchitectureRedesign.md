# 行情数据架构重新设计 v2.0

> **ℹ️**: 文中 MySQL 引用已于 2026-07 迁移到 PostgreSQL。`MysqlMarketDataLoader` 对应实现已改为 PG。

> 日期: 2026-06-13 | 状态: 评审通过
> 修订: v1.0 初稿 -> v2.0 评审修订

---

## 1. 分析总结

### 1.1 三套并存但仅一套活跃

| 编号 | 体系 | 命名空间 | 核心类型 | 状态 |
|------|------|---------|---------|------|
| A | 简单 Bar | domain::model | Bar (string symbol) | 死代码 - 全项目零引用 |
| B | 列式矩阵视图 | factor::compute | IMarketDataView 等 | 回测活跃 |
| C | KLine 体系 | astock::market | KLine, IDataProvider | 完全未使用 |

### 1.2 当前真实数据流

回测路径(已接通): JSON -> BacktestDataService -> CachedMarketDataView -> FactorEngine
QML行情(问题路径): QML -> DataService::fetchDataByType() -> MarketDataRepository(SQL) -> QVariantList -> QML

核心矛盾: QML行情通过DataService直接查MySQL返回QVariantList，绕过了IMarketDataView。

### 1.3 死代码清单

| 文件 | 原因 |
|------|------|
| src/domain/types/include/Bar.h | 零引用 |
| src/domain/data/* | 零引用 |
| src/domain/market/include/market/core/DataTypes.h | KLine体系无消费者 |
| src/domain/market/include/market/core/IDataProvider.h | 无实现 |
| src/domain/market/include/observers/ | 未接入 |

---

## 2. 架构重设计

### 2.1 核心决策: IMarketDataView 作为唯一行情数据契约

不为任何旧接口做兼容。factor::compute::IMarketDataView 已是成熟设计。

### 2.2 新增抽象: IMarketDataLoader (可扩展加载接口)

```cpp
namespace domain::market {
struct LoadSpec {
    vector<InstrumentId> instruments;
    DateRange dateRange;
    vector<string> extraFields;
    DateKey asOfDate{};       // 回测时间胶囊
    bool adjustFactor{false}; // 是否复权
};
class IMarketDataLoader {
    virtual unique_ptr<IMarketDataView> load(const LoadSpec&) = 0;
};
}
```

### 2.3 新增: AdjustableMarketDataView (复权装饰器)

包装任意 IMarketDataView，对 OHLCV 应用复权因子，不拷贝原始数据。

### 2.4 目标架构

```
消费者: FactorEngine / StrategyEngine / QML K线图
    |
    +--- IMarketDataView (列式矩阵)
    +--- HistoricalView (适配器)
    +--- MarketDataBridge (~150行 Qt桥接)
             loadBars() / subscribeRealtime()
    MarketDataFacade (编排/复权/线程安全)
    |
    +--- IMarketDataLoader (Mysql/未来Wind)
    +--- AdjustableMarketDataView
    +--- IMarketDataStream (Jujin/Sim)
```

### 2.5 分层职责

| 层 | 模块 | 行数估 | 职责 |
|----|------|--------|------|
| Domain | IMarketDataView | 已有 | 唯一行情契约 |
| Domain | IMarketDataStream | 已有 | 实时推送契约 |
| Domain | IMarketDataLoader | 新建 ~30 | 可扩展加载接口 |
| Domain | AdjustableMarketDataView | 新建 ~100 | 复权装饰器 |
| Infra | MarketDataRepository | 新建 ~150 | SQL查询封装 |
| Infra | SymbolMapper | 新建 ~120 | symbol<->InstrumentId<->symbol_id |
| Infra | MysqlMarketDataLoader | 新建 ~250 | MySQL->View构建器 |
| Facade | MarketDataFacade | 新建 ~250 | 编排/复权/线程安全 |
| Bridge | MarketDataBridge | 新建 ~150 | QVariant转换+推送 |

### 2.6 删除清单 (阶段8, grep确认零引用后)

| 文件 | 原因 |
|------|------|
| DataService.* | 替换为 MarketDataBridge |
| Bar.h | 死代码 |
| IMarketDataSource.* / CsvMarketDataSource.* | 死代码 |
| DataTypes.h (KLine) | 无消费者 |
| IDataProvider.h | 被 IMarketDataView+IMarketDataLoader 覆盖 |
| observers/ | 被 IMarketDataStream 替代 |

---

## 3. 实时推送设计

Bridge协议: subscribeRealtime() -> IMarketDataStream -> LiveMarketDataView -> onData() -> emit realtimeKline -> QML追加

合并逻辑: loadBars(历史)初始化K线序列 -> subscribeRealtime订阅实时 -> 每次tick追加到序列末尾

分页策略: QML请求超过5000根K线时只加载窗口前后各扩展一屏，滑动到边界时Bridge自动发起新分页请求

### 性能预算

| 场景 | 数据量 | 内存 | 加载时间 |
|------|--------|------|---------|
| QML K线(1股x500日) | 500行x5列 | ~10KB | <5ms |
| QML K线(1股x5000日) | 5000行x5列 | ~100KB | <10ms |
| 因子计算(5000股x2520日) | 12.6M cells | ~50MB | <2s |
| 分钟线(1股x30日) | 7200行x5列 | ~150KB | <10ms |

---

## 4. 回测支持

### 时间胶囊语义

LoadSpec::asOfDate: MySQL WHERE trade_date <= asOfDate，避免前视偏差。

### 迁移路径

| 阶段 | 方式 |
|------|------|
| 当前 | BacktestDataService + CachedMarketDataView(JSON) |
| 阶段4 | 同时支持 MysqlMarketDataLoader |
| 阶段6 | 默认MysqlMarketDataLoader，废弃JSON中间层 |

---

## 5. SymbolMapper

InstrumentId 作为领域层唯一标识。三向映射 (symbol <-> InstrumentId <-> symbol_id) 构造后不可变，天然线程安全。

---

## 6. 并发模型

- MarketDataFacade 线程安全单例
- loadViewAsync() 线程池执行，通过 QMetaObject::invokeMethod 回调回主线程
- loadView() 同步调用，使用连接池获取独立 MySQL 连接
- SymbolMapper 只读无需加锁
- LiveMarketDataView 使用 mutex 保护环形缓冲

---

## 7. 实施计划

### 阶段划分

| 阶段 | 内容 | 依赖 | 测试要求 |
|------|------|------|---------|
| 1 | MarketDataRepository | 无 | 单元测试: SQL查询正确性 |
| 2 | SymbolMapper | 阶段1 | 单元测试: 三向映射一致性 |
| 3 | IMarketDataLoader + MysqlMarketDataLoader | 阶段1,2 | 单元测试: 行->列式矩阵 |
| 4 | AdjustableMarketDataView | 阶段3 | 单元测试: 复权因子应用 |
| 5 | MarketDataFacade | 阶段3,4 | 集成测试: 历史/实时/混合 |
| 6 | MarketDataBridge | 阶段5 | 集成测试: QVariant转换 |
| 7 | QML适配 + DataService转发适配器 | 阶段6 | 功能测试: QML行情面板 |
| 8 | 删除旧文件(grep确认零引用) | 阶段7 | 全量编译+测试 |
| 9 | JujinMarketDataStream | 阶段5 | 集成测试(需掘金环境) |

### 新建文件

- src/domain/market/include/IMarketDataLoader.h
- src/domain/market/include/AdjustableMarketDataView.h + .cpp
- src/infrastructure/database/MarketDataRepository.h/.cpp
- src/infrastructure/database/SymbolMapper.h/.cpp
- src/infrastructure/database/MysqlMarketDataLoader.h/.cpp
- src/app/facade/MarketDataFacade.h/.cpp
- src/ui/bridge/include/MarketDataBridge.h + .cpp

### 修改文件

- src/app/src/AppBootstrap.cpp (注入 MarketDataFacade)
- src/infrastructure/CMakeLists.txt
- src/app/CMakeLists.txt

### DataService 平滑迁移

阶段7保留 DataService 作为转发适配器:
```cpp
class DataService : public QObject {
    Q_INVOKABLE QVariantList queryDailyBar(...) {
        qWarning("DataService is deprecated, use MarketDataBridge");
        return bridge_->loadBars(...);
    }
};
```
所有 QML 迁移完成后阶段8删除 DataService。

---

## 8. 验收标准

| # | 标准 | 验证方式 |
|---|------|---------|
| 1 | Bridge不含database->executeQuery() | 代码审查 |
| 2 | Bridge不含数据源选择逻辑(允许参数校验和类型转换) | 代码审查 |
| 3 | Bridge行数 <= 200 | 代码审查 |
| 4 | 所有数据通过 IMarketDataView 或 MarketDataRepository | 代码审查 |
| 5 | Domain层无 #include <QVariantMap> 等Qt头文件 | grep检查 |
| 6 | Bridge层无 #include <ISqlDatabase> | grep检查 |
| 7 | SymbolMapper覆盖所有已注册标的 | 单元测试 |
| 8 | 旧死代码删除前 grep 全量搜索确认零引用 | grep+全量编译 |
| 9 | MysqlMarketDataLoader 单元测试覆盖行->列式矩阵转换 | 单元测试 |
| 10 | MarketDataFacade 集成测试覆盖历史/实时/混合场景 | 集成测试 |
| 11 | QML行情面板通过 MarketDataBridge 正常工作 | 功能测试 |
| 12 | 回测通过 asOfDate 产出正确结果 | 回测测试 |

---

## 9. 附录：为什么不使用 KLine/IDataProvider

| 原因 | 说明 |
|------|------|
| 因子引擎用 IMarketDataView | 因子计算是核心，它消费列式矩阵。再造一套导致永久转换层 |
| IMarketDataView 更强大 | 支持任意字段访问 (pb_ratio)、零拷贝切片、列式矩阵(SIMD) |
| KLine 只有一条记录 | 无法表达 (date x symbol x field) 三维数据，不支持横截面查询 |
| 历史清白 | KLine/IDataProvider 从未接入任何模块，废弃零风险 |
| LiveMarketDataView 已正确 | 实时数据直接推入环形缓冲区，下游统一消费 IMarketDataView |

---

## 10. 评审修订记录

| 版本 | 日期 | 修订 |
|------|------|------|
| v1.0 | 2026-06-13 | 初稿：统一契约/删除死代码/5层架构 |
| v2.0 | 2026-06-13 | +IMarketDataLoader扩展接口 +AdjustableMarketDataView +实时Bridge协议 +asOfDate +分页性能预算 +并发模型 +单元测试要求 +DataService平滑迁移方案 +grep检查清单 +Bridge验收标准放宽 |
