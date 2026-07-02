#pragma once
// OrderRecorder — 实盘订单/成交持久化
// 同步直写, 不阻塞交易主路径 (DB 连接池复用)

#include <cstdint>
#include <string>

namespace astock::infrastructure::database {

// ── 枚举 (禁止字符串路由) ──
enum class RecSide     : std::uint8_t { Buy = 0, Sell = 1 };
enum class RecOrdType  : std::uint8_t { Limit = 0, Market = 1 };
enum class RecPosEff   : std::uint8_t { Open = 0, Close = 1 };
enum class RecOrdStatus: std::uint8_t { Pending = 0, PartiallyFilled = 1, Filled = 2, Cancelled = 3, Rejected = 4 };

class OrderRecorder {
public:
    static OrderRecorder& instance();

    // 同步写入, 返回受影响行数
    int insertOrder(const std::string& clOrdId, const std::string& strategyId,
                    const std::string& symbol,
                    RecSide side, RecOrdType orderType,
                    double price, int quantity, double signalScore,
                    RecPosEff positionEffect, int tradingDay);

    int updateOrderStatus(const std::string& clOrdId, RecOrdStatus status,
                          const std::string& brokerOrderId = "",
                          const std::string& message = "");

    int insertFill(const std::string& clOrdId, const std::string& brokerOrderId,
                   const std::string& execId, const std::string& symbol,
                   double fillPrice, int fillQty, double fillAmount,
                   double commission, int tradingDay, const std::string& fillTime);

private:
    OrderRecorder() = default;

    static const char* toStr(RecSide v);
    static const char* toStr(RecOrdType v);
    static const char* toStr(RecPosEff v);
    static const char* toStr(RecOrdStatus v);
};

} // namespace astock::infrastructure::database
