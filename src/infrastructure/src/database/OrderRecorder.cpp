#include "database/OrderRecorder.h"
#include "database/NativePgConnectionPool.h"
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
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) {
        INTERNAL_WARN_STREAM << "[OrderRecorder] DB 不可用, 订单记录丢失: " << clOrdId;
        return 0;
    }
    using astock::database::SqlParam;
    std::string sql =
        "INSERT INTO data.live_order "
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
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) return 0;
    using astock::database::SqlParam;
    std::string sql =
        "UPDATE data.live_order SET status=?, broker_order_id=IF(?<>'',?,broker_order_id), "
        "message=IF(?<>'',?,message) WHERE cl_ord_id=?";
    std::vector<SqlParam> params = {
        SqlParam{std::string(toStr(status))},
        SqlParam{brokerOrderId}, SqlParam{brokerOrderId},
        SqlParam{message}, SqlParam{message},
        SqlParam{clOrdId}
    };
    return db->executeUpdate(sql, params);
}

int OrderRecorder::updateOrderFill(const std::string& clOrdId, const std::string& execId,
                                    double fillPrice, int fillQty, double fillAmount,
                                    double commission, const std::string& fillTime) {
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) return 0;
    using astock::database::SqlParam;
    std::string sql =
        "UPDATE data.live_order SET status='FILLED', exec_id=?, fill_price=?, fill_qty=?, "
        "fill_amount=?, commission=?, fill_time=? WHERE cl_ord_id=?";
    std::vector<SqlParam> params = {
        SqlParam{execId},
        SqlParam{fillPrice}, SqlParam{static_cast<std::int32_t>(fillQty)},
        SqlParam{fillAmount}, SqlParam{commission},
        SqlParam{fillTime}, SqlParam{clOrdId}
    };
    return db->executeUpdate(sql, params);
}

int OrderRecorder::insertAccountSnapshot(int tradingDay, double totalAsset, double availableCash,
                                          double marketValue, double frozenCash,
                                          double realizedPnl, double unrealizedPnl) {
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) return 0;
    using astock::database::SqlParam;
    std::string sql =
        "INSERT INTO data.live_account_daily "
        "(trading_day, total_asset, available_cash, market_value, frozen_cash, "
        "realized_pnl, unrealized_pnl) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT (trading_day) DO UPDATE SET "
        "total_asset=EXCLUDED.total_asset, available_cash=EXCLUDED.available_cash, "
        "market_value=EXCLUDED.market_value, frozen_cash=EXCLUDED.frozen_cash, "
        "realized_pnl=EXCLUDED.realized_pnl, unrealized_pnl=EXCLUDED.unrealized_pnl";
    std::vector<SqlParam> params = {
        SqlParam{static_cast<std::int32_t>(tradingDay)},
        SqlParam{totalAsset}, SqlParam{availableCash},
        SqlParam{marketValue}, SqlParam{frozenCash},
        SqlParam{realizedPnl}, SqlParam{unrealizedPnl}
    };
    return db->executeUpdate(sql, params);
}

} // namespace astock::infrastructure::database
