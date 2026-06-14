#pragma once

#include "ISqlDatabase.h"

#include <memory>
#include <string>
#include <vector>

namespace astock::infrastructure::database {

// ═══ 值对象：日K线行 ═══

struct DailyBarRow {
    std::string symbol;
    std::string tradeDate;  // "YYYY-MM-DD"
    double open   = 0.0;
    double high   = 0.0;
    double low    = 0.0;
    double close  = 0.0;
    double volume = 0.0;
    double turnover = 0.0;
};

struct FieldRow {
    std::string symbol;
    std::string tradeDate;
    std::string fieldName;
    double value = 0.0;
};

// ═══ 行情数据仓储：封装所有行情相关 SQL 查询 ═══

class MarketDataRepository {
public:
    explicit MarketDataRepository(std::shared_ptr<astock::database::ISqlDatabase> db)
        : db_(std::move(db)) {}

    /// 日K线查询（单标的）
    std::vector<DailyBarRow> queryDailyBar(
        const std::string& symbol,
        const std::string& startDate,
        const std::string& endDate,
        const std::vector<std::string>& extraFields = {});

    /// 日K线查询（多标的批量）
    std::vector<DailyBarRow> queryDailyBarBatch(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate);

    /// 因子字段横截面查询
    std::vector<FieldRow> queryFieldCrossSection(
        const std::string& field,
        const std::string& date,
        const std::vector<std::string>& symbols = {});

    /// 指数成分股查询
    std::vector<std::string> queryIndexConstituents(
        const std::string& indexSymbol,
        const std::string& date);

    /// 下一个交易日查询
    std::string queryNextTradingDay(const std::string& anchorDate);

private:
    static DailyBarRow rowToBar(const astock::database::SqlQueryResultRow& row);

    std::shared_ptr<astock::database::ISqlDatabase> db_;
};

} // namespace astock::infrastructure::database