// DataSourceRegistry.h — 数据源类型注册中心（纯 C++，零 Qt 依赖）
#pragma once

#include <string>
#include <vector>
#include <unordered_set>

namespace cleaning {

/// @brief 数据源字段 Schema
struct FieldSchema {
    std::vector<std::string> names;          // 所有字段名（有序）
    std::unordered_set<std::string> numeric; // 数值类型字段（其余为字符串）
};

/// @brief 数据源抽象接口（每种数据类型实现一个子类）
class IDataSource {
public:
    virtual ~IDataSource() = default;

    /// @brief 类型标识（如 "kline_daily", "financial"）
    virtual std::string typeName() const = 0;
    /// @brief 数据库表名
    virtual std::string tableName() const = 0;
    /// @brief 日期列名
    virtual std::string dateColumn() const = 0;

    /// @brief 构建 GROUP BY 查询（用于标的汇总）
    virtual std::string buildGroupQuery(const std::string& startDate, const std::string& endDate) const = 0;
    /// @brief 构建全量数据查询
    virtual std::string buildDataQuery(const std::string& startDate, const std::string& endDate,
                                        const std::vector<std::string>& symbols) const = 0;
    /// @brief 从数据库连接探测字段 Schema（LIMIT 1）
    virtual FieldSchema detectSchema(void* dbConnection,
                                     const std::string& startDate, const std::string& endDate) const = 0;
};

/// @brief 日线数据源
class KlineDataSource : public IDataSource {
public:
    std::string typeName() const override { return "kline_daily"; }
    std::string tableName() const override { return "daily_bar"; }
    std::string dateColumn() const override { return "trade_date"; }

    std::string buildGroupQuery(const std::string& start, const std::string& end) const override {
        return "SELECT symbol, MIN(trade_date) AS start_dt, MAX(trade_date) AS end_dt, COUNT(*) AS cnt "
               "FROM daily_bar WHERE trade_date BETWEEN '" + start + "' AND '" + end + "' GROUP BY symbol";
    }

    std::string buildDataQuery(const std::string& start, const std::string& end,
                                const std::vector<std::string>& symbols) const override {
        std::string sql = "SELECT * FROM daily_bar WHERE trade_date BETWEEN '" + start + "' AND '" + end + "'";
        if (symbols.size() > 0 && symbols.size() <= 1000) {
            sql += " AND symbol IN (";
            for (size_t i = 0; i < symbols.size(); ++i) { if (i > 0) sql += ","; sql += "'" + symbols[i] + "'"; }
            sql += ")";
        }
        return sql;
    }

    FieldSchema detectSchema(void* /*db*/, const std::string&, const std::string&) const override {
        // 日线字段固定（性能优化：无需查数据库）
        FieldSchema s;
        s.names = {"symbol","trade_date","open","high","low","close","pre_close",
                   "volume","turnover","change_pct","change_amt","amplitude",
                   "turnover_rate","pe_ratio","pb_ratio","market_cap","circulating_market_cap",
                   "pre_adj_factor","post_adj_factor","data_source"};
        s.numeric = {"open","high","low","close","pre_close","volume","turnover","change_pct",
                     "change_amt","amplitude","turnover_rate","pe_ratio","pb_ratio",
                     "market_cap","circulating_market_cap","pre_adj_factor","post_adj_factor"};
        return s;
    }
};

/// @brief 财务数据源
class FinancialDataSource : public IDataSource {
public:
    std::string typeName() const override { return "financial"; }
    std::string tableName() const override { return "financial_indicator"; }
    std::string dateColumn() const override { return "report_date"; }

    std::string buildGroupQuery(const std::string& start, const std::string& end) const override {
        return "SELECT si.symbol, MIN(fi.report_date) AS start_dt, MAX(fi.report_date) AS end_dt, COUNT(*) AS cnt "
               "FROM financial_indicator fi JOIN symbol_info si ON fi.symbol_id=si.symbol_id "
               "WHERE fi.report_date BETWEEN '" + start + "' AND '" + end + "' GROUP BY si.symbol";
    }

    std::string buildDataQuery(const std::string& start, const std::string& end,
                                const std::vector<std::string>& /*symbols*/) const override {
        return "SELECT si.symbol, fi.* FROM financial_indicator fi "
               "JOIN symbol_info si ON fi.symbol_id=si.symbol_id "
               "WHERE fi.report_date BETWEEN '" + start + "' AND '" + end + "'";
    }

    FieldSchema detectSchema(void* /*db*/, const std::string&, const std::string&) const override {
        FieldSchema s;
        s.names = {"symbol","report_date",
            "eps","bps","roa","roe","profit_margin","gross_margin","operating_margin",
            "debt_to_equity","current_ratio","quick_ratio",
            "operating_cash_flow","investing_cash_flow","financing_cash_flow",
            "total_revenue","net_profit","total_assets","total_liabilities","equity",
            "dividend_yield","payout_ratio","dividend_stability",
            "effective_disclosure_date"};
        s.numeric = {"eps","bps","roa","roe","profit_margin","gross_margin","operating_margin",
            "debt_to_equity","current_ratio","quick_ratio",
            "operating_cash_flow","investing_cash_flow","financing_cash_flow",
            "total_revenue","net_profit","total_assets","total_liabilities","equity",
            "dividend_yield","payout_ratio","dividend_stability"};
        return s;
    }
};

/// @brief 根据类型名获取数据源实例
inline IDataSource* sourceByName(const std::string& name) {
    static KlineDataSource s_kline;
    static FinancialDataSource s_fin;
    if (name == "kline_daily" || name == "kline_weekly" || name == "kline_monthly" || name == "minute_data")
        return &s_kline;
    if (name == "financial") return &s_fin;
    return nullptr;
}

/// @brief 合并多个 Schema 为统一 Schema
inline FieldSchema mergeSchemas(const std::vector<FieldSchema>& schemas) {
    FieldSchema merged;
    std::unordered_set<std::string> seen;
    for (const auto& s : schemas) {
        for (const auto& f : s.names) {
            if (seen.insert(f).second) merged.names.push_back(f);
        }
        for (const auto& f : s.numeric) merged.numeric.insert(f);
    }
    return merged;
}

} // namespace cleaning
