# DatabaseService 使用示例

## 概述

`DatabaseService` 是一个通用的数据库服务单例类，集成了连接池管理和链式调用功能。它提供了简单易用的API，让开发者无需关心数据库连接的细节，直接进行数据库操作。

## 主要特性

1. **单例模式** - 全局唯一实例
2. **连接池管理** - 自动管理数据库连接池
3. **链式调用** - 支持流畅的链式查询构建
4. **多数据源支持** - 可以添加和管理多个数据源
5. **自动连接管理** - 自动重连和连接状态检查
6. **事务支持** - 完整的事务管理
7. **统计监控** - 连接和查询统计

## 快速开始

### 1. 初始化数据库连接

```cpp
#include "src/infrastructure/include/database/DatabaseService.h"

// 获取单例实例
auto& dbService = astock::database::DatabaseService::instance();

// 配置数据库连接
astock::database::DatabaseConfig config;
config.host = "localhost";
config.port = 3306;
config.database = "astock_quant";
config.username = "root";
config.password = "123456a";
config.charset = "utf8mb4";
config.pool_size = 3;

// 初始化默认数据库连接（使用连接池）
if (!dbService.initialize(config, true)) {
    // 处理初始化失败
}
```

### 2. 使用快捷宏（推荐）

```cpp
// 使用预定义的快捷宏
#define DB_SERVICE astock::database::DatabaseService::instance()
#define DB_QUERY(table) DB_SERVICE.from(table)
#define DB_EXECUTE(sql) DB_SERVICE.executeQuery(sql)
#define DB_UPDATE(sql) DB_SERVICE.executeUpdate(sql)

// 链式查询示例
auto& query = DB_QUERY("factors")
    .select("*")
    .where("major_category", ConditionType::EQUAL, "技术指标")
    .orderBy("create_date", OrderType::DESC)
    .limit(10);

QueryResult result = query.execute();

// 直接执行SQL
QueryResult countResult = DB_EXECUTE("SELECT COUNT(*) as count FROM factors");

// 执行更新
int affected = DB_UPDATE("UPDATE factors SET is_favorite = 1 WHERE factor_id = 'test'");
```

### 3. 添加和管理数据源

```cpp
// 添加备份数据源
astock::database::DatabaseConfig backupConfig;
backupConfig.host = "localhost";
backupConfig.port = 3306;
backupConfig.database = "astock_quant_backup";
backupConfig.username = "root";
backupConfig.password = "123456a";

if (dbService.addDataSource("backup", backupConfig, true)) {
    std::cout << "备份数据源添加成功" << std::endl;
}

// 使用指定数据源查询
auto& backupQuery = dbService.from("factors", "backup")
    .select("*")
    .limit(5);

QueryResult backupResult = backupQuery.execute();

// 获取数据源列表
auto dataSources = dbService.getDataSourceNames();
for (const auto& name : dataSources) {
    std::cout << "数据源: " << name << std::endl;
}
```

### 4. 事务操作

```cpp
// 开始事务
auto transaction = dbService.beginTransaction(false);
if (transaction) {
    try {
        // 执行多个操作
        int affected1 = DB_UPDATE("UPDATE factors SET status = 'active' WHERE status = 'pending'");
        int affected2 = DB_UPDATE("UPDATE factor_tags SET tag = 'updated' WHERE tag = 'old'");
        
        // 提交事务
        transaction->commit();
        std::cout << "事务提交成功，影响行数: " << (affected1 + affected2) << std::endl;
    } catch (const std::exception& e) {
        // 回滚事务
        transaction->rollback();
        std::cerr << "事务失败，已回滚: " << e.what() << std::endl;
    }
}
```

### 5. 完整的查询示例

```cpp
// 复杂的链式查询
try {
    auto& complexQuery = DB_QUERY("factors")
        .select("factor_id, factor_name, display_name, major_category, ic_value")
        .where("major_category", ConditionType::EQUAL, "技术指标")
        .andWhere("ic_value", ConditionType::GREATER_THAN, 0.5)
        .andWhere("status", ConditionType::EQUAL, "active")
        .orderBy("ic_value", OrderType::DESC)
        .orderBy("create_date", OrderType::DESC)
        .limit(20)
        .offset(0);
    
    // 查看生成的SQL
    std::cout << "生成的SQL: " << complexQuery.getRawSql().toStdString() << std::endl;
    
    // 执行查询
    QueryResult result = complexQuery.execute();
    
    // 处理结果
    for (const auto& row : result.getRows()) {
        std::cout << "因子ID: " << row.getString("factor_id").toStdString()
                  << ", 名称: " << row.getString("display_name").toStdString()
                  << ", IC值: " << row.getDouble("ic_value") << std::endl;
    }
    
} catch (const std::exception& e) {
    std::cerr << "查询失败: " << e.what() << std::endl;
}
```

### 6. 批量操作

```cpp
// 批量插入示例
std::vector<std::map<QString, QVariant>> batchData;
for (int i = 0; i < 100; ++i) {
    std::map<QString, QVariant> row;
    row["factor_id"] = QString("test_factor_%1").arg(i);
    row["factor_name"] = QString("测试因子%1").arg(i);
    row["display_name"] = QString("测试显示名称%1").arg(i);
    row["major_category"] = "测试分类";
    row["ic_value"] = 0.5 + (i * 0.01);
    batchData.push_back(row);
}

// 使用事务进行批量插入
auto transaction = dbService.beginTransaction(false);
if (transaction) {
    try {
        QString sql = "INSERT INTO factors (factor_id, factor_name, display_name, major_category, ic_value) "
                      "VALUES (:factor_id, :factor_name, :display_name, :major_category, :ic_value)";
        
        for (const auto& params : batchData) {
            int affected = dbService.executeUpdate(sql, params);
            if (affected <= 0) {
                throw std::runtime_error("插入失败");
            }
        }
        
        transaction->commit();
        std::cout << "批量插入成功，插入" << batchData.size() << "条记录" << std::endl;
        
    } catch (const std::exception& e) {
        transaction->rollback();
        std::cerr << "批量插入失败，已回滚: " << e.what() << std::endl;
    }
}
```

### 7. 连接状态管理

```cpp
// 检查连接状态
if (dbService.isConnected()) {
    std::cout << "默认数据库连接正常" << std::endl;
} else {
    std::cout << "默认数据库连接异常" << std::endl;
}

// 检查数据源连接状态
if (dbService.isDataSourceConnected("backup")) {
    std::cout << "备份数据源连接正常" << std::endl;
}

// 关闭所有连接
dbService.closeAll();
std::cout << "所有数据库连接已关闭" << std::endl;
```

## 与现有代码的对比

### 传统方式（需要手动管理连接）

```cpp
// 1. 创建数据库配置
DatabaseConfig config;
// ... 配置数据库

// 2. 创建数据库连接
auto database = std::make_shared<QtMySQLDatabase>(config, true);
if (!database->open()) {
    // 处理连接失败
}

// 3. 创建查询构建器
auto builder = createQueryBuilder(database);
if (!builder) {
    // 处理构建器创建失败
}

// 4. 构建查询
auto query = builder->from("factors")
    .select("*")
    .where("status", ConditionType::EQUAL, "active");

// 5. 执行查询
QueryResult result = query.execute();
```

### 使用DatabaseService（简化版）

```cpp
// 1. 初始化（只需一次）
auto& dbService = DatabaseService::instance();
dbService.initialize(config, true);

// 2. 直接使用链式调用
auto& query = DB_QUERY("factors")
    .select("*")
    .where("status", ConditionType::EQUAL, "active");

QueryResult result = query.execute();
```

## 最佳实践

1. **应用启动时初始化**：在应用程序启动时初始化`DatabaseService`
2. **使用快捷宏**：推荐使用`DB_QUERY`、`DB_EXECUTE`、`DB_UPDATE`宏简化代码
3. **合理使用事务**：对于多个相关操作，使用事务确保数据一致性
4. **及时关闭连接**：应用程序退出时调用`closeAll()`关闭所有连接
5. **错误处理**：始终对数据库操作进行异常处理
6. **连接池配置**：根据应用负载合理配置连接池大小

## 注意事项

1. `DatabaseService`是线程安全的，可以在多线程环境中使用
2. 连接池会自动管理连接的创建和回收
3. 如果连接断开，`DatabaseService`会尝试自动重连
4. 使用事务时，确保在异常情况下进行回滚
5. 批量操作时，建议使用事务提高性能和数据一致性

## 总结

`DatabaseService`提供了一个简单、统一、高效的数据库操作接口，将连接池管理、链式调用、事务处理等功能封装在一个单例类中。开发者可以专注于业务逻辑，而无需关心底层的数据库连接细节。