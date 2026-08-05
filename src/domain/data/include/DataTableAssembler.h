#pragma once
// DataTableAssembler — 纯 C++ Arrow Table 构建器
// 将 SQL 查询结果 + 财务缓存 + 指数映射 组装为 Arrow Table
// 零 Qt 依赖，零 I/O

#include <arrow/api.h>

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "database/ISqlDatabase.h"

namespace domain::data {

class DataTableAssembler final {
public:
    /// @brief 财务缓存: symbol → [(disclosure_date, {column: value})]
    using FinancialCache = std::map<std::string,
        std::vector<std::pair<std::string, std::unordered_map<std::string, std::string>>>>;

    /// @brief 指数映射: symbol → index_code
    using IndexCodeMap = std::map<std::string, std::string>;

    /// @brief 从 SQL 行构建 Arrow Table
    /// @param rows   MarketDataRepository 查询结果
    /// @param allFields  统一 Schema 的全部字段名（有序）
    /// @param numericFields  数值类型字段集
    /// @param finCache  财务缓存（按 symbol + disclosure_date 索引）
    /// @param indexMap  指数成分映射
    /// @return Arrow Table，由调用方写入 DataCache
    static std::shared_ptr<arrow::Table> buildFromSqlRows(
        const std::vector<astock::database::SqlQueryResultRow>& rows,
        const std::vector<std::string>& allFields,
        const std::unordered_set<std::string>& numericFields,
        const FinancialCache& finCache,
        const IndexCodeMap& indexMap);
};

} // namespace domain::data
