# API 接口冻结文档

## 概述
本文档记录已冻结的API接口。冻结的接口表示其公共API已经稳定，不应再添加新的公共方法或修改现有方法的签名。如需扩展功能，应通过其他方式实现（如新增类、使用组合模式等）。

## 冻结时间
- 初始冻结：2026年3月12日
- 本次扩展冻结：2026年4月5日（交易桥接接口）

## 冻结原则
1. **向后兼容**：冻结的接口必须保持向后兼容
2. **功能完整**：接口应提供完整的功能集
3. **使用广泛**：接口已被多个模块使用
4. **测试充分**：接口经过充分测试

## 已冻结接口列表

### 1. 数据库连接管理接口
**文件**: `src/ui/bridge/include/DatabaseConnectionManager.h`

```cpp
// FROZEN INTERFACE - 数据库连接管理器
class DatabaseConnectionManager {
public:
    // 单例访问
    static DatabaseConnectionManager& instance();
    
    // 初始化数据库连接
    bool initialize();
    
    // 获取数据库连接
    std::shared_ptr<QtMySQLDatabase> getDatabase();
    
    // 检查连接状态
    bool isConnected() const;
    
    // 获取最后错误信息
    QString getLastError() const;
    
    // 冻结说明：数据库连接管理核心接口已稳定
    // 如需扩展，请创建新的DatabaseService类
};
```

### 2. 因子服务接口
**文件**: `src/ui/bridge/include/FactorService.h`

```cpp
// FROZEN INTERFACE - 因子服务
class FactorService : public QObject {
    Q_OBJECT
public:
    // 因子CRUD操作
    QString addFactor(const QVariantMap& factorData);
    bool updateFactor(const QString& factorId, const QVariantMap& factorData);
    bool deleteFactor(const QString& factorId);
    QVariantMap getFactorById(const QString& factorId);
    QVariantList getAllFactors();
    
    // 因子查询和过滤
    QVariantList getFactorsByType(const QString& type);
    QVariantList searchFactors(const QString& keyword);
    QVariantList filterFactorsByCategory(const QString& category);
    QVariantList filterFactorsByTags(const QStringList& tags);
    
    // 因子导入导出
    bool importFactors(const QVariantList& factors);
    bool exportFactors(const QString& format, const QString& filePath);
    
    // 收藏功能
    bool toggleFavorite(const QString& factorId);
    
    // 数据同步
    void syncWithDatabase();
    void clearCache();

    // 冻结说明：因子服务核心接口已稳定
    // 如需扩展因子分析功能，请创建FactorAnalysisService类
};
```

### 3. 数据获取控制器接口
**文件**: `src/ui/bridge/include/DataFetchController.h`

```cpp
// FROZEN INTERFACE - 数据获取控制器
class DataFetchController : public QObject {
    Q_OBJECT
public:
    // 数据获取操作
    void fetchData();
    void cancelFetch();
    void clearData();
    
    // 数据库操作
    void loadFromDatabase(const QString& symbol, 
                         const QString& startDate, 
                         const QString& endDate);
    void saveToDatabase();
    
    // 数据清洗
    QVariantList cleanData(const QVariantList& data, const QVariantMap& rules);
    void cleanDataAsync(const QVariantMap& rules);
    
    // 缓存管理
    QVariantList getAllCacheKeys();
    QVariantList getAllDataSetInfos();
    void loadFromCache(const QString& cacheKey);
    void loadDataSetById(int dataId);
    
    // 属性设置器
    void setDataSource(const QString& source);
    void setSymbols(const QStringList& symbols);
    void setStartDate(const QString& date);
    void setEndDate(const QString& date);
    void setDataType(const QString& type);
    
    // 冻结说明：数据获取核心接口已稳定
    // 如需扩展数据源类型，请通过setDataSource方法配置
};
```

### 4. 因子视图模型接口
**文件**: `src/ui/bridge/include/FactorViewModel.h`

```cpp
// FROZEN INTERFACE - 因子视图模型
class FactorViewModel : public QAbstractListModel {
    Q_OBJECT
public:
    // QAbstractListModel接口
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    
    // 数据更新
    void updateData(const QVariantList& factors);
    void clearData();
    
    // 因子操作
    void addFactor(const QVariantMap& factor);
    void updateFactor(int index, const QVariantMap& factor);
    void removeFactor(int index);
    
    // 过滤和排序
    void filterByCategory(const QString& category);
    void filterByTags(const QStringList& tags);
    void sortByField(const QString& field, bool ascending = true);
    
    // 冻结说明：视图模型核心接口已稳定
    // 如需扩展显示字段，请修改roleNames()方法
};
```

### 5. QtMySQL数据库接口
**文件**: `src/infrastructure/include/database/QtMySQLDatabase.h`

```cpp
// FROZEN INTERFACE - Qt MySQL数据库包装器
class QtMySQLDatabase {
public:
    // 连接管理
    bool open();
    void close();
    bool isOpen() const;
    
    // 查询执行
    QueryResult executeQuery(const QString& sql, 
                            const std::map<QString, QVariant>& params = {});
    int executeUpdate(const QString& sql,
                     const std::map<QString, QVariant>& params = {});
    int executeBatchUpdate(const QString& sql,
                          const std::vector<std::map<QString, QVariant>>& batchParams);
    
    // 事务管理
    std::unique_ptr<TransactionGuard> beginTransaction(bool autoCommit = true);
    
    // 数据库信息
    bool tableExists(const QString& tableName);
    QueryResult getTableSchema(const QString& tableName);
    QString getDatabaseVersion();
    
    // 统计信息
    ConnectionStats getConnectionStats() const;
    void resetStats();
    
    // 配置
    void setQueryTimeout(int milliseconds);
    void setConnectionOptions(const std::map<QString, QString>& options);
    
    // 冻结说明：数据库操作核心接口已稳定
    // 如需扩展存储过程支持，请创建新的DatabaseProcedure类
};
```

### 6. 行情服务接口
**文件**: `src/ui/bridge/include/MarketDataService.h`

```cpp
// FROZEN INTERFACE - 行情服务
class MarketDataService : public QObject {
    Q_OBJECT
public:
    static MarketDataService* instance();

    Q_INVOKABLE void initialize();
    Q_INVOKABLE bool isInitialized() const;
    Q_INVOKABLE QVariantList marketSnapshots() const;
    Q_INVOKABLE QString primarySymbol() const;
    Q_INVOKABLE bool hasLiveData() const;
    Q_INVOKABLE void setWatchlist(const QStringList& symbols);
    Q_INVOKABLE void ensureWatchSymbol(const QString& symbol);
    Q_INVOKABLE QVariantMap resolveInstrument(const QString& query) const;

    // 冻结说明：交易页与嵌入式闪电交易组件已依赖该接口
    // 如需增加更深档位或更多订阅策略，请扩展内部事件与快照结构，不修改对外签名
};
```

### 7. 持仓账户服务接口
**文件**: `src/ui/bridge/include/PositionAccountService.h`

```cpp
// FROZEN INTERFACE - 持仓账户服务
class PositionAccountService : public QObject {
    Q_OBJECT
public:
    static PositionAccountService* instance();

    Q_INVOKABLE void initialize();
    Q_INVOKABLE bool isInitialized() const;
    Q_INVOKABLE QVariantList positions() const;
    Q_INVOKABLE QVariantMap accountSnapshot() const;
    Q_INVOKABLE QVariantList recentOrderStatuses() const;

    // 冻结说明：QML 交易页、持仓页、订单列表已直接消费这组属性
    // 后续只能增强字段内容，不能改名或改返回形态
};
```

### 9. 策略创建/编辑/删除/查询桥接接口
**文件**: `src/ui/bridge/include/StrategyBridig.h`, `src/ui/bridge/src/StrategyBridig.cpp`

```cpp
// FROZEN INTERFACE - 策略 CRUD 桥接
class StrategyBridig : public QObject {
    Q_OBJECT
public:
    // 冻结签名：不允许改名、改参、改返回值
    Q_INVOKABLE QString add(const QVariantMap& payload);
    Q_INVOKABLE bool update(const QVariantMap& payload);
    Q_INVOKABLE bool remove(const QString& strategyId);
    Q_INVOKABLE QVariantMap get(const QString& strategyId);
};
```

冻结约束（2026-05-31 生效）：
- `add/update` 顶层 `payload` 白名单字段固定为：
  - `strategyId`
  - `strategyName`
  - `strategyTypeIndex`
  - `strategyBehaviorKind`
  - `description`
  - `assetTypeIndex`
  - `timeFrameIndex`
  - `riskLevelIndex`
  - `optimization_method`
  - `parameters`
  - `tags`
  - `status`
  - `factorIds`
  - `ruleIds`
- 白名单外字段一律报错拒绝（fail-fast），不做兼容映射。
- `update/remove/get` 的 `strategyId` 必须是合法 UUID，不允许空值或旧别名字段回退。
- `parameters` 必须包含 `rule_profile` 与 `rule_composer_state` 对象，旧字段（如 `commonConfig`、`strategySpec`、`rule_template_bindings`）保持拒绝。

冻结说明：
- 该接口已成为策略创建页、编辑流程与策略库删除/读取流程的稳定边界。
- 后续如需扩展字段，必须走“新版本接口”或单独新桥接类，不得在现有签名和白名单上直接扩容。

### 8. 交易执行服务接口
**文件**: `src/ui/bridge/include/TradeExecutionService.h`

```cpp
// FROZEN INTERFACE - 交易执行服务
class TradeExecutionService : public QObject {
    Q_OBJECT
public:
    static TradeExecutionService* instance();

    Q_INVOKABLE void initialize();
    Q_INVOKABLE bool isInitialized() const;
    Q_INVOKABLE QString lastErrorMessage() const;
    Q_INVOKABLE QVariantList recentOrders() const;
    Q_INVOKABLE void clearRecentOrders();
    Q_INVOKABLE bool submitBridgeOrder(const QVariantMap& request);
    Q_INVOKABLE bool submitManualTestOrder(const QString& symbol,
                                           const QString& side,
                                           double price,
                                           qint64 quantity = 100,
                                           const QString& orderType = QStringLiteral("LIMIT"),
                                           const QString& strategyId = QStringLiteral("manual_test"),
                                           const QString& strategyName = QStringLiteral("Manual Test"));
    Q_INVOKABLE bool cancelManualTestOrder(const QString& orderId);

    // 冻结说明：交易页当前所有真实/回退下单入口都已绑定该接口
    // 如需支持新业务动作，请通过 request/runtime metadata 扩展，不修改公共方法签名
};
```

### 9. 交易连接配置服务接口
**文件**: `src/ui/bridge/include/TradingConnectionConfigService.h`

```cpp
// FROZEN INTERFACE - 交易连接配置服务
class TradingConnectionConfigService : public QObject {
    Q_OBJECT
public:
    static TradingConnectionConfigService* instance();

    Q_INVOKABLE void initialize();
    Q_INVOKABLE QVariantMap loadConfiguration();
    Q_INVOKABLE QVariantMap defaultConfiguration() const;
    Q_INVOKABLE QStringList defaultClientProcessNames() const;
    Q_INVOKABLE bool saveConfiguration(const QVariantMap& configuration);
    Q_INVOKABLE void refreshClientProcessStatus();

    // 冻结说明：交易连接配置页与启动链已依赖该配置模型
    // 后续允许在 currentConfiguration 内新增键，不允许更改已有键的语义
};
```

### 10. 掘金交易门面接口
**文件**: `src/ui/bridge/include/JujinApi.h`

```cpp
// FROZEN INTERFACE - 掘金交易门面
class JujinApi {
public:
    bool initialize(const ConfigParams& config);
    bool connect();
    bool disconnect();
    bool is_connected() const;
    bool is_initialized() const;

    std::string place_order(const std::string& symbol,
                            OrderSide side,
                            OrderType type,
                            double price,
                            double quantity,
                            const std::string& client_order_id = std::string(),
                            const std::map<std::string, std::string>& metadata = {});
    bool cancel_order(const std::string& order_id);
    bool subscribe_market_data(const std::vector<std::string>& symbols,
                               MarketDataType type,
                               const std::map<std::string, std::string>& options = {});

    std::vector<Position> query_positions();
    AccountInfo query_account();
    OrderResult query_order(const std::string& order_id);
    std::vector<OrderResult> query_orders(const std::string& symbol = "",
                                          const std::string& status = "",
                                          int limit = 100);

    void set_event_bus(std::shared_ptr<engine::EventBus> bus);
    std::string last_error_message() const;
    void clear_error();
    ConfigParams get_config() const;
    bool check_connection() const;
    std::string get_connection_status() const;

    // 冻结说明：这是 bridge 层唯一允许直接触达 GM/掘金会话的门面
    // 扩展真实交易能力时优先新增 metadata / ConfigParams 字段，不改现有方法语义
};
```

### 11. 交易运行时管理器接口
**文件**: `src/ui/bridge/include/TradingRuntimeManager.h`

```cpp
// FROZEN INTERFACE - 交易运行时管理器
class TradingRuntimeManager {
public:
    static TradingRuntimeManager& instance();

    void set_event_bus(std::shared_ptr<engine::EventBus> event_bus);
    std::shared_ptr<GmStrategySession> create_session(const ConfigParams& config);
    std::shared_ptr<GmStrategySession> get_session(const std::string& account_id) const;
    bool start_session(const std::string& account_id);
    bool stop_session(const std::string& account_id);
    void stop_all_sessions();
    std::vector<TradingSessionSnapshot> session_snapshots() const;
    size_t active_session_count() const;

    // 冻结说明：对外只冻结 manager 入口，不冻结 GmStrategySession 内部实现细节
};
```

### 12. 冻结边界说明
以下类型当前不纳入冻结范围：

1. `GmStrategySession`：属于交易运行时内部实现，最近仍在频繁调整回报补查、执行回报聚合和状态归一化。
2. `EventFormat` 事件负载细节：字段可增不减，允许继续为真实交易链补充 metadata。
3. QML 内部 helper 函数：属于展示实现，不作为稳定 API 承诺。

## 接口扩展指南

### 允许的修改
1. **Bug修复**：修复接口实现中的错误
2. **性能优化**：优化内部实现，不改变接口
3. **日志增强**：增加调试日志，不改变接口
4. **错误处理改进**：改进错误信息，不改变接口签名

### 禁止的修改
1. **添加新的公共方法**：应创建新类
2. **修改方法签名**：包括参数类型、返回类型
3. **删除公共方法**：必须保持向后兼容
4. **改变方法语义**：方法的行为不应改变

### 扩展模式
1. **装饰器模式**：包装现有接口添加功能
2. **策略模式**：通过配置改变行为
3. **工厂模式**：创建接口的不同实现
4. **组合模式**：组合多个接口提供新功能

## 当前重复功能扫描（2026-04-05）

### A. 交易订单状态归一化逻辑重复
**文件**:
- `src/ui/bridge/src/PositionAccountService.cpp`
- `src/ui/bridge/src/TradeExecutionService.cpp`

**当前状态**:
- 已于 2026-04-05 收敛到共享工具 `OrderRuntimeUtils`。
- 两处服务现在只保留不同的空状态策略，不再各自维护一套状态机实现。

**重复点**:
1. 状态标准化：`normalizeOrderStatus()` 与 `normalizeRecentOrderStatus()`
2. 由成交进度反推状态：`resolveOrderStatusFromProgress()` 与 `resolveRecentOrderStatusFromProgress()`
3. 状态 phase 比较：`orderStatusPhase()` 与 `recentOrderStatusPhase()`
4. 回退保护：`shouldIgnoreOrderStatusRegression()` 与 `shouldIgnoreOrderRecordRegression()`

**判断**:
- 该项已从“高重复”降为“已收敛”。
- 后续若再改交易状态机，应只改共享工具。

**建议**:
- 提炼为共享的 `OrderStateUtils` 或匿名 namespace 公共 helper，统一状态枚举、phase、回退规则。

### B. 订单身份判定与缓存合并逻辑重复
**文件**:
- `src/ui/bridge/src/PositionAccountService.cpp`
- `src/ui/bridge/src/TradeExecutionService.cpp`

**当前状态**:
- 已于 2026-04-05 收敛到共享工具 `OrderRecordUtils`。
- 同单识别、按编号查找、recent-order upsert 已共用一套实现；两处服务只保留各自的等价判定字段差异。

**重复点**:
1. `orderStatusMatchesId()` 与 `findRecentOrderRecord()` / `orderRecordsReferToSameOrder()`
2. `appendOrderStatus()` 与 `appendRecentOrder()` 的“同单识别 + 抗回退 + 去重插入”流程

**判断**:
- 该项已从“高重复”降为“局部差异保留”。
- 当前剩余差异主要是 PositionAccountService 与 TradeExecutionService 对“等价记录”的字段要求不同，这属于业务差异，不再是基础设施重复。

**建议**:
- 抽出统一的 order identity matcher 和 recent-order merger；PositionAccountService 与 TradeExecutionService 只保留各自字段转换。

### C. 服务层与 ViewModel 层能力重叠
**文件**:
- `src/ui/bridge/include/FactorService.h`
- `src/ui/bridge/include/FactorViewModel.h`
- `src/ui/bridge/include/StrategyService.h`
- `src/ui/bridge/include/StrategyViewModel.h`

**当前状态**:
- 已于 2026-04-05 部分收敛。
- 查询/搜索/过滤能力现在统一以下沉到 Service 为准，ViewModel 同名接口仅保留兼容委托，不再维护独立筛选逻辑。
- ViewModel 继续承担列表模型、增量更新和展示字段适配职责。

**重复点**:
1. 因子：`getFactorById/getAllFactors/search/filter/update/remove`
2. 策略：`getStrategyById/getAllStrategies/search/filter/update/remove/status/performance`

**判断**:
- 该项已从“职责重叠”降为“兼容层保留”。
- 当前剩余重复主要是 ViewModel 还保留了兼容 Q_INVOKABLE 入口，但查询逻辑本身已不再双写。

**建议**:
- 后续若清理兼容层，可逐步让 QML 只调用 Service 查询接口，ViewModel 只保留模型适配和行级更新接口。

### D. 数据库连接能力存在双入口
**文件**:
- `src/ui/bridge/include/DatabaseConnectionManager.h`
- `src/ui/bridge/include/DatabaseConnectionService.h`

**重复点**:
1. 连接建立/关闭
2. 状态查询
3. 数据库实例获取

**判断**:
- 这是历史演进留下的双入口。
- 当前 `DatabaseConnectionManager` 已被冻结并且仍被旧桥接代码依赖，`DatabaseConnectionService` 更像新抽象。

**建议**:
- 维持 manager 兼容层不动，新代码统一优先走 service；确认无旧依赖后再考虑把 manager 退化成 facade。

## 例外情况
如需修改冻结接口，必须：
1. 提交RFC（Request For Comments）文档
2. 获得项目维护者批准
3. 更新所有使用该接口的代码
4. 更新本文档

## 版本历史
- v1.0 (2026-03-12): 初始冻结，包含5个核心接口
- v1.1 (2026-04-05): 扩展冻结交易桥接公共接口，并补充当前重复功能扫描

## 维护者
- 项目核心开发团队

## 相关文档
- [数据库服务使用示例](./DatabaseService使用示例.md)
- [因子库实现总结](./因子库实现总结.md)
- [通用组件方案评估与改进建议](./通用组件方案评估与改进建议.md)