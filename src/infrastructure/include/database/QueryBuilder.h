// QueryBuilder.h - Database query builder interface
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <QVariant>
#include <QString>

#include "QtMySQLDatabase.h"

namespace astock {
namespace database {

// Forward declarations
class QueryResult;
struct QueryResultRow;

// Query types
enum class QueryType {
    Query_SELECT,
    Query_INSERT,
    Query_UPDATE,
    Query_DELETE
};

// Condition types
enum class ConditionType {
    TYPES_EQUAL,
    TYPES_NOT_EQUAL,
    TYPES_GREATER_THAN,
    TYPES_GREATER_EQUAL,
    TYPES_LESS_THAN,
    TYPES_LESS_EQUAL,
    TYPES_LIKE,
    TYPES_IN,
    TYPES_BETWEEN,
    TYPES_IS_NULL,
    TYPES_IS_NOT_NULL
};

// Order types
enum class OrderType {
    Order_ASC,
    Order_DESC
};

// Query condition structure
struct QueryCondition {
    QString column;
    ConditionType conditionType;
    QVariant value;
    QVariant value2;  // For BETWEEN conditions
    
    QueryCondition(const QString& col, ConditionType t, const QVariant& val)
        : column(col), conditionType(t), value(val) {}
    
    QueryCondition(const QString& col, ConditionType t, const QVariant& val1, const QVariant& val2)
        : column(col), conditionType(t), value(val1), value2(val2) {}
};

// Query builder class - supports fluent interface
class QueryBuilder {
public:
    QueryBuilder(std::shared_ptr<QtMySQLDatabase> database);
    ~QueryBuilder();
    
    // Fluent interface methods
    QueryBuilder& from(const QString& table);
    QueryBuilder& select(const QString& columns = "*");
    QueryBuilder& select(const std::vector<QString>& columns);
    QueryBuilder& where(const QString& column, ConditionType type, const QVariant& value);
    QueryBuilder& where(const QString& column, ConditionType type, const QVariant& value1, const QVariant& value2);
    QueryBuilder& andWhere(const QString& column, ConditionType type, const QVariant& value);
    QueryBuilder& orWhere(const QString& column, ConditionType type, const QVariant& value);
    QueryBuilder& orderBy(const QString& column, OrderType order = OrderType::Order_ASC);
    QueryBuilder& groupBy(const QString& column);
    QueryBuilder& limit(int count);
    QueryBuilder& offset(int count);
    QueryBuilder& join(const QString& table, const QString& onCondition, const QString& joinType = "INNER");
    QueryBuilder& having(const QString& condition);
    
    // Execute queries
    QueryResult execute();
    int executeUpdate();
    int executeInsert();
    int executeDelete();
    int count();
    bool exists();
    
    // Get generated SQL (with parameter placeholders)
    QString getSql() const;
    
    // Get generated SQL (with actual values, ready to execute)
    QString getRawSql() const;
    
    // Reset builder
    void reset();
    
private:
    // Private helper methods
    QString buildSelectSql() const;
    QString buildWhereClause() const;
    std::map<QString, QVariant> buildParameters() const;
    QString conditionTypeToString(ConditionType type) const;
    QString orderTypeToString(OrderType order) const;
    
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
};

// Factory function
std::shared_ptr<QueryBuilder> createQueryBuilder(std::shared_ptr<QtMySQLDatabase> database);

} // namespace database
} // namespace astock