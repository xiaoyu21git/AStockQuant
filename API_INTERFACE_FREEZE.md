# API 接口冻结文档

## 概述
本文档记录已冻结的API接口。冻结的接口表示其公共API已经稳定，不应再添加新的公共方法或修改现有方法的签名。如需扩展功能，应通过其他方式实现（如新增类、使用组合模式等）。

## 冻结时间
2026年3月12日

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

## 例外情况
如需修改冻结接口，必须：
1. 提交RFC（Request For Comments）文档
2. 获得项目维护者批准
3. 更新所有使用该接口的代码
4. 更新本文档

## 版本历史
- v1.0 (2026-03-12): 初始冻结，包含5个核心接口

## 维护者
- 项目核心开发团队

## 相关文档
- [数据库服务使用示例](./DatabaseService使用示例.md)
- [因子库实现总结](./因子库实现总结.md)
- [通用组件方案评估与改进建议](./通用组件方案评估与改进建议.md)