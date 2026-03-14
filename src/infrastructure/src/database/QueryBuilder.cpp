// QueryBuilder.cpp - 数据库查询链式调用接口实现
#include "database/QueryBuilder.h"
#include "database/QtMySQLDatabase.h"

#include <QDebug>
#include <sstream>
#include <algorithm>
#include <stdexcept>

namespace astock {
namespace database {

QueryBuilder::QueryBuilder(std::shared_ptr<QtMySQLDatabase> database)
    : m_database(database)
    , m_queryType(QueryType::Query_SELECT)
    , m_limit(-1)
    , m_offset(-1)
    , m_distinct(false)
{
    reset();
}

QueryBuilder::~QueryBuilder()
{
}

QueryBuilder& QueryBuilder::from(const QString& table)
{
    m_table = table;
    return *this;
}

QueryBuilder& QueryBuilder::select(const QString& columns)
{
    m_queryType = QueryType::Query_SELECT;
    if (columns == "*") {
        m_columns.clear();
    } else {
        // 解析逗号分隔的列名
        QStringList columnList = columns.split(',', Qt::SkipEmptyParts);
        for (const QString& col : columnList) {
            m_columns.push_back(col.trimmed());
        }
    }
    return *this;
}

QueryBuilder& QueryBuilder::select(const std::vector<QString>& columns)
{
    m_queryType = QueryType::Query_SELECT;
    m_columns = columns;
    return *this;
}

QueryBuilder& QueryBuilder::where(const QString& column, ConditionType type, const QVariant& value)
{
    m_conditions.emplace_back(column, type, value);
    return *this;
}

QueryBuilder& QueryBuilder::where(const QString& column, ConditionType type, const QVariant& value1, const QVariant& value2)
{
    m_conditions.emplace_back(column, type, value1, value2);
    return *this;
}

QueryBuilder& QueryBuilder::andWhere(const QString& column, ConditionType type, const QVariant& value)
{
    // 当前实现中，所有条件都是AND关系
    return where(column, type, value);
}

QueryBuilder& QueryBuilder::orWhere(const QString& column, ConditionType type, const QVariant& value)
{
    // 简化实现：在当前条件后添加OR条件
    // 实际项目中需要更复杂的逻辑处理OR条件
    qWarning() << "OR conditions are not fully supported in this simplified implementation";
    return where(column, type, value);
}

QueryBuilder& QueryBuilder::orderBy(const QString& column, OrderType order)
{
    m_orderBy.emplace_back(column, order);
    return *this;
}

QueryBuilder& QueryBuilder::groupBy(const QString& column)
{
    m_groupBy.push_back(column);
    return *this;
}

QueryBuilder& QueryBuilder::limit(int count)
{
    m_limit = count;
    return *this;
}

QueryBuilder& QueryBuilder::offset(int count)
{
    m_offset = count;
    return *this;
}

QueryBuilder& QueryBuilder::join(const QString& table, const QString& onCondition, const QString& joinType)
{
    QString joinClause = QString("%1 JOIN %2 ON %3").arg(joinType).arg(table).arg(onCondition);
    m_joins.push_back(joinClause);
    return *this;
}

QueryBuilder& QueryBuilder::having(const QString& condition)
{
    m_having = condition;
    return *this;
}

QueryResult QueryBuilder::execute()
{
    if (m_table.isEmpty()) {
        throw QtMySQLException("Table name is not specified");
    }
    
    QString sql = buildSelectSql();
    std::map<QString, QVariant> params = buildParameters();
    
    qDebug() << "Executing query:" << sql;
    if (!params.empty()) {
        qDebug() << "Query parameters:";
        for (const auto& [key, value] : params) {
            qDebug() << "  " << key << "=" << value;
        }
        qDebug() << "Raw SQL with values:" << getRawSql();
    }
    
    try {
        return m_database->executeQuery(sql, params);
    } catch (const std::exception& e) {
        qCritical() << "Query execution failed:" << e.what();
        throw;
    }
}

int QueryBuilder::executeUpdate()
{
    // 简化实现，实际项目中需要完整的UPDATE语句构建
    qWarning() << "executeUpdate() is not implemented in this simplified version";
    return 0;
}

int QueryBuilder::executeInsert()
{
    // 简化实现，实际项目中需要完整的INSERT语句构建
    qWarning() << "executeInsert() is not implemented in this simplified version";
    return 0;
}

int QueryBuilder::executeDelete()
{
    // 简化实现，实际项目中需要完整的DELETE语句构建
    qWarning() << "executeDelete() is not implemented in this simplified version";
    return 0;
}

int QueryBuilder::count()
{
    // 构建COUNT查询
    QString originalTable = m_table;
    std::vector<QString> originalColumns = m_columns;
    
    m_columns.clear();
    m_columns.push_back("COUNT(*) as count");
    
    try {
        QueryResult result = execute();
        if (!result.isEmpty()) {
            return result.getRow(0).getInt("count");
        }
        return 0;
    } catch (...) {
        // 恢复原始状态
        m_table = originalTable;
        m_columns = originalColumns;
        throw;
    }
}

bool QueryBuilder::exists()
{
    return count() > 0;
}

QString QueryBuilder::getSql() const
{
    return buildSelectSql();
}

void QueryBuilder::reset()
{
    m_queryType = QueryType::Query_SELECT;
    m_table.clear();
    m_columns.clear();
    m_conditions.clear();
    m_orderBy.clear();
    m_groupBy.clear();
    m_joins.clear();
    m_having.clear();
    m_limit = -1;
    m_offset = -1;
    m_distinct = false;
}

QString QueryBuilder::buildSelectSql() const
{
    if (m_table.isEmpty()) {
        throw QtMySQLException("Table name is not specified");
    }
    
    std::ostringstream oss;
    
    // SELECT子句
    oss << "SELECT ";
    if (m_distinct) {
        oss << "DISTINCT ";
    }
    
    if (m_columns.empty()) {
        oss << "*";
    } else {
        for (size_t i = 0; i < m_columns.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << m_columns[i].toStdString();
        }
    }
    
    // FROM子句
    oss << " FROM " << m_table.toStdString();
    
    // JOIN子句
    for (const QString& join : m_joins) {
        oss << " " << join.toStdString();
    }
    
    // WHERE子句
    QString whereClause = buildWhereClause();
    if (!whereClause.isEmpty()) {
        oss << " WHERE " << whereClause.toStdString();
    }
    
    // GROUP BY子句
    if (!m_groupBy.empty()) {
        oss << " GROUP BY ";
        for (size_t i = 0; i < m_groupBy.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << m_groupBy[i].toStdString();
        }
    }
    
    // HAVING子句
    if (!m_having.isEmpty()) {
        oss << " HAVING " << m_having.toStdString();
    }
    
    // ORDER BY子句
    if (!m_orderBy.empty()) {
        oss << " ORDER BY ";
        for (size_t i = 0; i < m_orderBy.size(); ++i) {
            if (i > 0) oss << ", ";
            const auto& [column, order] = m_orderBy[i];
            oss << column.toStdString() << " " << orderTypeToString(order).toStdString();
        }
    }
    
    // LIMIT和OFFSET子句
    if (m_limit > 0) {
        oss << " LIMIT " << m_limit;
        if (m_offset > 0) {
            oss << " OFFSET " << m_offset;
        }
    }
    
    return QString::fromStdString(oss.str());
}

QString QueryBuilder::buildWhereClause() const
{
    if (m_conditions.empty()) {
        return QString();
    }
    
    std::ostringstream oss;
    
    for (size_t i = 0; i < m_conditions.size(); ++i) {
        const QueryCondition& cond = m_conditions[i];
        
        if (i > 0) {
            oss << " AND ";
        }
        
        oss << cond.column.toStdString() << " " << conditionTypeToString(cond.conditionType).toStdString();
        
        switch (cond.conditionType) {
            case ConditionType::TYPES_IS_NULL:
            case ConditionType::TYPES_IS_NOT_NULL:
                // 不需要值
                break;
            case ConditionType::TYPES_BETWEEN:
                // 对于日期字段，使用标准的参数名：start_date 和 end_date
                if (cond.column.compare("trade_date", Qt::CaseInsensitive) == 0) {
                    oss << " :start_date AND :end_date";
                } else {
                    oss << " :" << cond.column.toStdString() << "_start AND :" << cond.column.toStdString() << "_end";
                }
                break;
            case ConditionType::TYPES_IN:
                // 简化处理，实际项目中需要处理多个值
                oss << " (:" << cond.column.toStdString() << ")";
                break;
            default:
                oss << " :" << cond.column.toStdString();
                break;
        }
    }
    
    return QString::fromStdString(oss.str());
}

std::map<QString, QVariant> QueryBuilder::buildParameters() const
{
    std::map<QString, QVariant> params;
    
    for (const QueryCondition& cond : m_conditions) {
        QString columnName = cond.column;
        
        switch (cond.conditionType) {
            case ConditionType::TYPES_IS_NULL:
            case ConditionType::TYPES_IS_NOT_NULL:
                // 不需要参数
                break;
            case ConditionType::TYPES_BETWEEN:
                // 对于trade_date字段，使用标准的参数名：start_date 和 end_date
                if (columnName.compare("trade_date", Qt::CaseInsensitive) == 0) {
                    params[":start_date"] = cond.value;
                    params[":end_date"] = cond.value2;
                } else {
                    params[":" + columnName + "_start"] = cond.value;
                    params[":" + columnName + "_end"] = cond.value2;
                }
                break;
            case ConditionType::TYPES_IN:
                // 简化处理，实际项目中需要处理多个值
                params[":" + columnName] = cond.value;
                break;
            default:
                params[":" + columnName] = cond.value;
                break;
        }
    }
    
    return params;
}

QString QueryBuilder::conditionTypeToString(ConditionType type) const
{
    switch (type) {
        case ConditionType::TYPES_EQUAL: return "=";
        case ConditionType::TYPES_NOT_EQUAL: return "!=";
        case ConditionType::TYPES_GREATER_THAN: return ">";
        case ConditionType::TYPES_GREATER_EQUAL: return ">=";
        case ConditionType::TYPES_LESS_THAN: return "<";
        case ConditionType::TYPES_LESS_EQUAL: return "<=";
        case ConditionType::TYPES_LIKE: return "LIKE";
        case ConditionType::TYPES_IN: return "IN";
        case ConditionType::TYPES_BETWEEN: return "BETWEEN";
        case ConditionType::TYPES_IS_NULL: return "IS NULL";
        case ConditionType::TYPES_IS_NOT_NULL: return "IS NOT NULL";
        default: return "=";
    }
}

QString QueryBuilder::orderTypeToString(OrderType order) const
{
    switch (order) {
        case OrderType::Order_ASC: return "ASC";
        case OrderType::Order_DESC: return "DESC";
        default: return "ASC";
    }
}

// 获取生成的SQL（带实际值，可直接执行）
QString QueryBuilder::getRawSql() const
{
    if (m_table.isEmpty()) {
        throw QtMySQLException("Table name is not specified");
    }
    
    std::ostringstream oss;
    
    // SELECT子句
    oss << "SELECT ";
    if (m_distinct) {
        oss << "DISTINCT ";
    }
    
    if (m_columns.empty()) {
        oss << "*";
    } else {
        for (size_t i = 0; i < m_columns.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << m_columns[i].toStdString();
        }
    }
    
    // FROM子句
    oss << " FROM " << m_table.toStdString();
    
    // JOIN子句
    for (const QString& join : m_joins) {
        oss << " " << join.toStdString();
    }
    
    // WHERE子句（带实际值）
    if (!m_conditions.empty()) {
        oss << " WHERE ";
        
        for (size_t i = 0; i < m_conditions.size(); ++i) {
            const QueryCondition& cond = m_conditions[i];
            
            if (i > 0) {
                oss << " AND ";
            }
            
            oss << cond.column.toStdString() << " " << conditionTypeToString(cond.conditionType).toStdString();
            
            switch (cond.conditionType) {
                case ConditionType::TYPES_IS_NULL:
                case ConditionType::TYPES_IS_NOT_NULL:
                    // 不需要值
                    break;
                case ConditionType::TYPES_BETWEEN:
                    // 对于日期字段，直接嵌入值
                    oss << " '" << cond.value.toString().toStdString() << "' AND '" << cond.value2.toString().toStdString() << "'";
                    break;
                case ConditionType::TYPES_IN:
                    // 简化处理
                    oss << " ('" << cond.value.toString().toStdString() << "')";
                    break;
                default:
                    if (cond.value.type() == QVariant::String) {
                        oss << " '" << cond.value.toString().toStdString() << "'";
                    } else if (cond.value.type() == QVariant::Int || cond.value.type() == QVariant::LongLong || 
                               cond.value.type() == QVariant::UInt || cond.value.type() == QVariant::ULongLong) {
                        oss << " " << cond.value.toLongLong();
                    } else if (cond.value.type() == QVariant::Double) {
                        oss << " " << cond.value.toDouble();
                    } else {
                        oss << " '" << cond.value.toString().toStdString() << "'";
                    }
                    break;
            }
        }
    }
    
    // GROUP BY子句
    if (!m_groupBy.empty()) {
        oss << " GROUP BY ";
        for (size_t i = 0; i < m_groupBy.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << m_groupBy[i].toStdString();
        }
    }
    
    // HAVING子句
    if (!m_having.isEmpty()) {
        oss << " HAVING " << m_having.toStdString();
    }
    
    // ORDER BY子句
    if (!m_orderBy.empty()) {
        oss << " ORDER BY ";
        for (size_t i = 0; i < m_orderBy.size(); ++i) {
            if (i > 0) oss << ", ";
            const auto& [column, order] = m_orderBy[i];
            oss << column.toStdString() << " " << orderTypeToString(order).toStdString();
        }
    }
    
    // LIMIT和OFFSET子句
    if (m_limit > 0) {
        oss << " LIMIT " << m_limit;
        if (m_offset > 0) {
            oss << " OFFSET " << m_offset;
        }
    }
    
    return QString::fromStdString(oss.str());
}

// 工厂函数实现
std::shared_ptr<QueryBuilder> createQueryBuilder(std::shared_ptr<QtMySQLDatabase> database)
{
    return std::make_shared<QueryBuilder>(database);
}

} // namespace database
} // namespace astock
