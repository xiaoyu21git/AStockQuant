// DataSourceRegistry.h — 数据源类型注册中心（纯 C++，零 Qt 依赖）
// 所有数据类型的字段定义唯一定点，禁止其它文件写字面量字段名
#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <sstream>

namespace cleaning {

/// @brief 数据源字段 Schema
struct FieldSchema {
    std::vector<std::string> names;          // 所有字段名（有序，决定 Arrow 列顺序）
    std::unordered_set<std::string> numeric; // 数值类型字段（其余为字符串）
};

// ═══════════════════════════════════════════════════════════════════
// 字段名常量 — 唯一出现点，其余文件引用 detectSchema() 获取
// ═══════════════════════════════════════════════════════════════════

namespace kline_columns {
// 日线 20 列（来自 daily_bar 表，排除 id/created_at/updated_at 审计字段）
inline const std::vector<std::string>& names() {
    static const std::vector<std::string> v = {
        "symbol","trade_date",
        "open","high","low","close","pre_close",
        "volume","turnover","change_pct","change_amt","amplitude",
        "turnover_rate","pe_ratio","pb_ratio",
        "market_cap","circulating_market_cap",
        "pre_adjust_factor","post_adjust_factor","data_source"
    };
    return v;
}
inline const std::unordered_set<std::string>& numeric() {
    static const std::unordered_set<std::string> s = {
        "open","high","low","close","pre_close",
        "volume","turnover","change_pct","change_amt","amplitude",
        "turnover_rate","pe_ratio","pb_ratio",
        "market_cap","circulating_market_cap",
        "pre_adjust_factor","post_adjust_factor"
    };
    return s;
}
// SQL SELECT 片段（禁止 SELECT * 带出审计字段）
inline std::string sqlSelect() {
    return "s.symbol,d.trade_date,"
           "d.open,d.high,d.low,d.close,d.pre_close,"
           "d.volume,d.turnover,d.change_pct,d.change_amt,d.amplitude,"
           "d.turnover_rate,d.pe_ratio,d.pb_ratio,"
           "d.market_cap,d.circulating_market_cap,"
           "d.pre_adjust_factor,d.post_adjust_factor,d.data_source";
}
} // namespace kline_columns

namespace symbol_info_columns {
// symbol_info 元数据 6 列（JOIN 到 K线行）
inline const std::vector<std::string>& names() {
    static const std::vector<std::string> v = {
        "name","exchange","industry_code",
        "list_date","delist_date","status"
    };
    return v;
}
inline const std::unordered_set<std::string>& numeric() {
    static const std::unordered_set<std::string> s = {"industry_code"};
    return s;
}
inline std::string sqlSelect() {
    return "s.name,s.exchange,"
           "s.industry_code,"
           "s.list_date,s.delist_date,s.status";
}
} // namespace symbol_info_columns

namespace index_columns {
// 指数成分 1 列
inline const std::vector<std::string>& names() {
    static const std::vector<std::string> v = {"index_code"};
    return v;
}
inline const std::unordered_set<std::string>& numeric() {
    static const std::unordered_set<std::string> s = {};
    return s;
}
} // namespace index_columns

namespace financial_columns {
// 财务 25 列（来自 financial_indicator 表，排除 indicator_id/symbol_id/created_at/updated_at）
// effective_disclosure_date 映射为 disclosure_date
inline const std::vector<std::string>& names() {
    static const std::vector<std::string> v = {
        "symbol","report_date","report_type",
        "eps","bps","roa","roe",
        "profit_margin","gross_margin","operating_margin",
        "debt_to_equity","current_ratio","quick_ratio",
        "operating_cash_flow","investing_cash_flow","financing_cash_flow",
        "total_revenue","net_profit","total_assets","total_liabilities","equity",
        "dividend_yield","payout_ratio","dividend_stability",
        "disclosure_date"
    };
    return v;
}
inline const std::unordered_set<std::string>& numeric() {
    static const std::unordered_set<std::string> s = {
        "eps","bps","roa","roe",
        "profit_margin","gross_margin","operating_margin",
        "debt_to_equity","current_ratio","quick_ratio",
        "operating_cash_flow","investing_cash_flow","financing_cash_flow",
        "total_revenue","net_profit","total_assets","total_liabilities","equity",
        "dividend_yield","payout_ratio","dividend_stability"
    };
    return s;
}
// SQL SELECT 片段 — 禁止 fi.* 带出审计字段
inline std::string sqlSelect() {
    return "si.symbol,fi.report_date,fi.report_type,"
           "fi.effective_disclosure_date AS disclosure_date,"
           "fi.eps,fi.bps,fi.roa,fi.roe,"
           "fi.profit_margin,fi.gross_margin,fi.operating_margin,"
           "fi.debt_to_equity,fi.current_ratio,fi.quick_ratio,"
           "fi.operating_cash_flow,fi.investing_cash_flow,fi.financing_cash_flow,"
           "fi.total_revenue,fi.net_profit,fi.total_assets,fi.total_liabilities,fi.equity,"
           "fi.dividend_yield,fi.payout_ratio,fi.dividend_stability";
}
} // namespace financial_columns

namespace news_sentiment_columns {
// 新闻舆情列（来自 data.news_sentiment 表）
inline const std::vector<std::string>& names() {
    static const std::vector<std::string> v = {
        "symbol","publish_time",
        "sentiment_score","social_sentiment","investor_sentiment","market_sentiment"
    };
    return v;
}
inline const std::unordered_set<std::string>& numeric() {
    static const std::unordered_set<std::string> s = {
        "sentiment_score","social_sentiment","investor_sentiment","market_sentiment"
    };
    return s;
}
inline std::string sqlSelect() {
    return "si.symbol,ns.publish_time,"
           "ns.sentiment_score,ns.social_sentiment,"
           "ns.investor_sentiment,ns.market_sentiment";
}
} // namespace news_sentiment_columns

namespace policy_data_columns {
// 政策数据列（来自 fund.policy_data 表）
inline const std::vector<std::string>& names() {
    static const std::vector<std::string> v = {
        "symbol","publish_time","policy_score"
    };
    return v;
}
inline const std::unordered_set<std::string>& numeric() {
    static const std::unordered_set<std::string> s = {"policy_score"};
    return s;
}
inline std::string sqlSelect() {
    return "si.symbol,pd.publish_time,pd.policy_score";
}
} // namespace policy_data_columns

namespace alternative_data_columns {
// 另类数据列（来自 fund.alternative_data 表）
inline const std::vector<std::string>& names() {
    static const std::vector<std::string> v = {
        "symbol","trade_date","hot_rank","basis_rate"
    };
    return v;
}
inline const std::unordered_set<std::string>& numeric() {
    static const std::unordered_set<std::string> s = {"hot_rank","basis_rate"};
    return s;
}
inline std::string sqlSelect() {
    return "si.symbol,ad.trade_date,ad.hot_rank,ad.basis_rate";
}
} // namespace alternative_data_columns

namespace minute_bar_columns {
// 分钟线列（来自 mkt.minute_bar 表）
inline const std::vector<std::string>& names() {
    static const std::vector<std::string> v = {
        "symbol","trade_ts","open","high","low","close","volume","amount"
    };
    return v;
}
inline const std::unordered_set<std::string>& numeric() {
    static const std::unordered_set<std::string> s = {
        "open","high","low","close","volume","amount"
    };
    return s;
}
inline std::string sqlSelect() {
    return "si.symbol,mb.trade_ts,"
           "mb.open,mb.high,mb.low,mb.close,mb.volume,mb.amount";
}
} // namespace minute_bar_columns

// ═══════════════════════════════════════════════════════════════════
// 数据源接口与实现
// ═══════════════════════════════════════════════════════════════════

class IDataSource {
public:
    virtual ~IDataSource() = default;

    virtual std::string typeName() const = 0;
    virtual std::string tableName() const = 0;
    virtual std::string dateColumn() const = 0;

    virtual std::string buildGroupQuery(const std::string& startDate,
                                        const std::string& endDate) const = 0;
    virtual std::string buildDataQuery(const std::string& startDate,
                                       const std::string& endDate,
                                       const std::vector<std::string>& symbols) const = 0;
    virtual FieldSchema detectSchema(void* dbConnection,
                                     const std::string& startDate,
                                     const std::string& endDate) const = 0;
};

/// @brief 日线数据源
class KlineDataSource : public IDataSource {
public:
    std::string typeName() const override { return "kline_daily"; }
    std::string tableName() const override { return "mkt.daily_bar"; }
    std::string dateColumn() const override { return "trade_date"; }

    std::string buildGroupQuery(const std::string& start,
                                const std::string& end) const override {
        std::ostringstream sql;
        sql << "SELECT si.symbol, MIN(d.trade_date) AS start_dt, MAX(d.trade_date) AS end_dt, COUNT(*) AS cnt "
            << "FROM mkt.daily_bar d JOIN ref.symbol_info si ON d.symbol_id = si.id "
            << "WHERE d.trade_date BETWEEN '" << start << "' AND '" << end
            << "' GROUP BY si.symbol";
        return sql.str();
    }

    std::string buildDataQuery(const std::string& start, const std::string& end,
                               const std::vector<std::string>& symbols) const override {
        // 显式列出 20 列 + JOIN symbol_info 拿 6 列元数据，禁止 SELECT *
        std::ostringstream sql;
        sql << "SELECT " << kline_columns::sqlSelect() << ","
            << symbol_info_columns::sqlSelect()
            << " FROM mkt.daily_bar d"
            << " JOIN ref.symbol_info s ON d.symbol_id = s.id"
            << " WHERE d.trade_date BETWEEN '" << start << "' AND '" << end << "'";
        if (!symbols.empty() && symbols.size() <= 2000) {
            sql << " AND s.symbol IN (";
            for (size_t i = 0; i < symbols.size(); ++i) {
                if (i > 0) sql << ",";
                sql << "'" << symbols[i] << "'";
            }
            sql << ")";
        }
        return sql.str();
    }

    FieldSchema detectSchema(void*, const std::string&, const std::string&) const override {
        FieldSchema s;
        s.names = kline_columns::names();
        s.numeric = kline_columns::numeric();
        return s;
    }
};

/// @brief 财务数据源
class FinancialDataSource : public IDataSource {
public:
    std::string typeName() const override { return "financial"; }
    std::string tableName() const override { return "fund.financial_indicator_daily"; }
    std::string dateColumn() const override { return "report_date"; }

    std::string buildGroupQuery(const std::string& start,
                                const std::string& end) const override {
        std::ostringstream sql;
        sql << "SELECT si.symbol, MIN(fi.report_date) AS start_dt,"
            << " MAX(fi.report_date) AS end_dt, COUNT(*) AS cnt "
            << "FROM fund.financial_indicator_daily fi JOIN ref.symbol_info si ON fi.symbol_id = si.id "
            << "WHERE fi.report_date BETWEEN '" << start << "' AND '" << end
            << "' GROUP BY si.symbol";
        return sql.str();
    }

    std::string buildDataQuery(const std::string& start, const std::string& end,
                               const std::vector<std::string>& symbols) const override {
        // 显式列出 25 列，禁止 fi.*
        std::ostringstream sql;
        sql << "SELECT " << financial_columns::sqlSelect()
            << " FROM fund.financial_indicator_daily fi"
            << " JOIN ref.symbol_info si ON fi.symbol_id = si.id"
            << " WHERE fi.report_date BETWEEN '" << start << "' AND '" << end << "'";
        if (!symbols.empty() && symbols.size() <= 2000) {
            sql << " AND si.symbol IN (";
            for (size_t i = 0; i < symbols.size(); ++i) {
                if (i > 0) sql << ",";
                sql << "'" << symbols[i] << "'";
            }
            sql << ")";
        }
        return sql.str();
    }

    FieldSchema detectSchema(void*, const std::string&, const std::string&) const override {
        FieldSchema s;
        s.names = financial_columns::names();
        s.numeric = financial_columns::numeric();
        return s;
    }
};

enum class DataSourceType { Kline, Financial, Unknown };

inline DataSourceType sourceTypeFromName(const std::string& name) {
    if (name == "kline_daily" || name == "kline_weekly"
        || name == "kline_monthly" || name == "minute_data")
        return DataSourceType::Kline;
    if (name == "financial") return DataSourceType::Financial;
    return DataSourceType::Unknown;
}

/// @brief 根据类型名获取数据源实例
inline IDataSource* sourceByName(const std::string& name) {
    static KlineDataSource s_kline;
    static FinancialDataSource s_fin;
    switch (sourceTypeFromName(name)) {
    case DataSourceType::Kline:     return &s_kline;
    case DataSourceType::Financial: return &s_fin;
    default:                       return nullptr;
    }
}

/// @brief 根据类型名返回仅该类型的 Schema（不含 symbol_info 元数据）
inline FieldSchema typeSchema(const std::string& name) {
    auto* src = sourceByName(name);
    return src ? src->detectSchema(nullptr, {}, {}) : FieldSchema{};
}

/// @brief 获取各数据类型独立的字段 Schema（用于查询）
inline FieldSchema typeSchemaWithMeta(const std::string& name) {
    auto* src = sourceByName(name);
    if (!src) return {};
    FieldSchema s = src->detectSchema(nullptr, {}, {});
    if (sourceTypeFromName(name) == DataSourceType::Kline) {
        // K线 JOIN 了 symbol_info，合并元数据字段
        for (auto& f : symbol_info_columns::names()) s.names.push_back(f);
    }
    return s;
}

/// @brief 合并多个 Schema 为统一 Arrow Schema（含通用元数据 + 指数成分）
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

/// @brief 根据用户选择的类型列表，返回合并后的完整 Schema
///        自动附加 symbol_info 元数据和 index_code
inline FieldSchema fullSchemaForTypes(const std::vector<std::string>& typeNames) {
    std::vector<FieldSchema> parts;
    for (const auto& t : typeNames) {
        auto* src = sourceByName(t);
        if (!src) continue;
        FieldSchema s = src->detectSchema(nullptr, {}, {});
        // K线查询时 JOIN 了 symbol_info，附加元数据列
        if (sourceTypeFromName(t) == DataSourceType::Kline) {
            for (auto& n : symbol_info_columns::names()) s.names.push_back(n);
        }
        // 财务查询时也通过 si.symbol JOIN 了 symbol_info
        // 但财务行不 JOIN 元数据列，元数据从 K线行带入
        parts.push_back(std::move(s));
    }
    // 添加通用元数据 + 指数代码
    FieldSchema meta;
    meta.names = symbol_info_columns::names();
    meta.numeric = symbol_info_columns::numeric();
    parts.push_back(meta);
    FieldSchema idx;
    idx.names = index_columns::names();
    idx.numeric = index_columns::numeric();
    parts.push_back(idx);

    return mergeSchemas(parts);
}

} // namespace cleaning
