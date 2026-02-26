// QueryBuilder.h - 数据库查询链式调用接口
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <QVariant>
#include <QString>

#include "database/QtMySQLDatabase.h"

namespace astock {
namespace database {

// 查询结果类型
enum class QueryType {
    SELECT,
    INSERT,
    UPDATE,
    DELETE,
    COUNT,
    EXISTS
};

// 查询条件类型
enum class ConditionType {
    EQUAL,
    NOT_EQUAL,
    GREATER_THAN,
    GREATER_EQUAL,
    LESS_THAN,
    LESS_EQUAL,
    LIKE,
    IN,
    BETWEEN,
    IS_NULL,
    IS_NOT_NULL
};

// 排序类型
enum class OrderType {
    ASC,
    DESC
};

// 查询条件
struct QueryCondition {
    QString column;
    ConditionType type;
    QVariant value;
    QVariant value2; // 用于BETWEEN条件
    
    QueryCondition(const QString& col, ConditionType t, const QVariant& val = QVariant())
        : column(col), type(t), value(val) {}
    
    QueryCondition(const QString& col, ConditionType t, const QVariant& val1, const QVariant& val2)
        : column(col), type(t), value(val1), value2(val2) {}
};

// 查询构建器类 - 支持链式调用
class QueryBuilder {
public:
    QueryBuilder(std::shared_ptr<QtMySQLDatabase> database);
    ~QueryBuilder();
    
    // 链式调用方法
    QueryBuilder& from(const QString& table);
    QueryBuilder& select(const QString& columns = "*");
    QueryBuilder& select(const std::vector<QString>& columns);
    QueryBuilder& where(const QString& column, ConditionType type, const QVariant& value);
    QueryBuilder& where(const QString& column, ConditionType type, const QVariant& value1, const QVariant& value2);
    QueryBuilder& andWhere(const QString& column, ConditionType type, const QVariant& value);
    QueryBuilder& orWhere(const QString& column, ConditionType type, const QVariant& value);
    QueryBuilder& orderBy(const QString& column, OrderType order = OrderType::ASC);
    QueryBuilder& groupBy(const QString& column);
    QueryBuilder& limit(int count);
    QueryBuilder& offset(int count);
    QueryBuilder& join(const QString& table, const QString& onCondition, const QString& joinType = "INNER");
    QueryBuilder& having(const QString& condition);
    
    // 执行查询
    QueryResult execute();
    int executeUpdate();
    int executeInsert();
    int executeDelete();
    int count();
    bool exists();
    
    // 获取生成的SQL（带参数占位符）
    QString getSql() const;
    
    // 获取生成的SQL（带实际值，可直接执行）
    QString getRawSql() const;
    
    // 重置构建器
    void reset();
    
private:
    std::shared_ptr<QtMySQLDatabase> m_database;
    QueryType m_queryType;
    QString m_table;
    std::vector<QString> m_columns;
    std::vector<QueryCondition> m_conditions;
    std::vector<std::pair<QString, OrderType>> m_orderBy;
    std::vector<QString> m_groupBy;
    std::vector<QString> m_joins;
    QString m_having;
    int m_limit;
    int m_offset;
    bool m_distinct;
    
    // 构建SQL语句
    QString buildSelectSql() const;
    QString buildWhereClause() const;
    std::map<QString, QVariant> buildParameters() const;
    
    // 辅助方法
    QString conditionTypeToString(ConditionType type) const;
    QString orderTypeToString(OrderType order) const;
};

// 工厂函数
std::shared_ptr<QueryBuilder> createQueryBuilder(std::shared_ptr<QtMySQLDatabase> database);

} // namespace database
} // namespace astock