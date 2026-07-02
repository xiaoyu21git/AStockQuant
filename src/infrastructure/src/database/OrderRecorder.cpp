#include "database/OrderRecorder.h"
#include "database/NativeMySQLConnectionPool.h"
#include "database/ISqlDatabase.h"
#include "foundation/log/logging.hpp"

namespace astock::infrastructure::database {

OrderRecorder& OrderRecorder::instance() {
    static OrderRecorder s;
    return s;
}

// ── 枚举 → SQL 字符串 ──

const char* OrderRecorder::toStr(RecSide v) {
    switch (v) {
        case RecSide::Buy:  return "BUY";
        case RecSide::Sell: return "SELL";
        default: return "BUY";
    }
}

const char* OrderRecorder::toStr(RecOrdType v) {
    switch (v) {
        case RecOrdType::Limit:  return "LIMIT";
        case RecOrdType::Market: return "MARKET";
        default: return "LIMIT";
    }
}

const char* OrderRecorder::toStr(RecPosEff v) {
    switch (v) {
        case RecPosEff::Open:  return "OPEN";
        case RecPosEff::Close: return "CLOSE";
        default: return "OPEN";
    }
}

const char* OrderRecorder::toStr(RecOrdStatus v) {
    switch (v) {
        case RecOrdStatus::Pending:          return "PENDING";
        case RecOrdStatus::PartiallyFilled:  return "PARTIAL_FILLED";
        case RecOrdStatus::Filled:           return "FILLED";
        case RecOrdStatus::Cancelled:        return "CANCELLED";
        case RecOrdStatus::Rejected:         return "REJECTED";
        default: return "PENDING";
    }
}

// ── 同步写入 ──

int OrderRecorder::insertOrder(const std::string& clOrdId, const std::string& strategyId,
                                const std::string& symbol,
                                RecSide side, RecOrdType orderType,
                                double price, int quantity, double signalScore,
                                RecPosEff positionEffect, int tradingDay) {
    auto db = astock::database::NativeMySQLConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) {
        INTERNAL_WARN_STREAM << "[OrderRecorder] DB 不可用, 订单记录丢失: " << clOrdId;
        return 0;
    }
    using astock::database::SqlParam;
    std::string sql =
        "INSERT INTO live_order "
        "(cl_ord_id, strategy_id, symbol, side, order_type, price, quantity, "
        "signal_score, position_effect, trading_day, status) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'PENDING')";
    std::vector<SqlParam> params = {
        SqlParam{clOrdId}, SqlParam{strategyId}, SqlParam{symbol},
        SqlParam{std::string(toStr(side))}, SqlParam{std::string(toStr(orderType))},
        SqlParam{price}, SqlParam{static_cast<std::int32_t>(quantity)},
        SqlParam{signalScore}, SqlParam{std::string(toStr(positionEffect))},
        SqlParam{static_cast<std::int32_t>(tradingDay)}
    };
    int ret = db->executeUpdate(sql, params);
    if (ret <= 0) {
        INTERNAL_WARN_STREAM << "[OrderRecorder] insertOrder 失败: " << clOrdId;
    }
    return ret;
}

int OrderRecorder::updateOrderStatus(const std::string& clOrdId, RecOrdStatus status,
                                      const std::string& brokerOrderId,
                                      const std::string& message) {
    auto db = astock::database::NativeMySQLConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) return 0;
    using astock::database::SqlParam;
    std::string sql =
        "UPDATE live_order SET status=?, broker_order_id=IF(?<>'',?,broker_order_id), "
        "message=IF(?<>'',?,message) WHERE cl_ord_id=?";
    std::vector<SqlParam> params = {
        SqlParam{std::string(toStr(status))},
        SqlParam{brokerOrderId}, SqlParam{brokerOrderId},
        SqlParam{message}, SqlParam{message},
        SqlParam{clOrdId}
    };
    return db->executeUpdate(sql, params);
}

int OrderRecorder::insertFill(const std::string& clOrdId, const std::string& brokerOrderId,
                               const std::string& execId, const std::string& symbol,
                               double fillPrice, int fillQty, double fillAmount,
                               double commission, int tradingDay, const std::string& fillTime) {
    auto db = astock::database::NativeMySQLConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) {
        INTERNAL_WARN_STREAM << "[OrderRecorder] DB 不可用, 成交记录丢失: " << clOrdId;
        return 0;
    }
    using astock::database::SqlParam;
    std::string sql =
        "INSERT INTO live_fill "
        "(cl_ord_id, broker_order_id, exec_id, symbol, fill_price, fill_qty, "
        "fill_amount, commission, trading_day, fill_time) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    std::vector<SqlParam> params = {
        SqlParam{clOrdId}, SqlParam{brokerOrderId}, SqlParam{execId},
        SqlParam{symbol},
        SqlParam{fillPrice}, SqlParam{static_cast<std::int32_t>(fillQty)},
        SqlParam{fillAmount}, SqlParam{commission},
        SqlParam{static_cast<std::int32_t>(tradingDay)}, SqlParam{fillTime}
    };
    int ret = db->executeUpdate(sql, params);
    if (ret <= 0) {
        INTERNAL_WARN_STREAM << "[OrderRecorder] insertFill 失败: " << clOrdId;
    }
    return ret;
}

} // namespace astock::infrastructure::database
