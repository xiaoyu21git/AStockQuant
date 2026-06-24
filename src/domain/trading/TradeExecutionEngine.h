#pragma once
// ---------------------------------------------------------------------------
// TradeExecutionEngine -- domain trading execution engine
// pure C++, zero Qt
//
// 4-stage pipeline: validate → scheduling conflict → risk → gateway submit
// Replaces all QVariantMap, #if ASTOCK_ENABLE_JUJIN, 3 split paths
// ---------------------------------------------------------------------------

#include "IBrokerGateway.h"
#include "TradingTypes.h"
#include "../strategy/include/RiskEvaluator.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace domain::trading {

// ── Enums ──
enum class OrderValidationCode : int {
    Ok = 0,
    InvalidSymbol,
    InvalidSide,
    InvalidPrice,
    InvalidQuantity,
    MissingRequiredFields,
};

enum class ActionKind : int {
    Normal = 0,
    CashRepay,
    ShareReturn,
    OptionExercise,
};

enum class ScheduleConflictCode : int {
    None = 0,
    PendingConflictingOrder,
    ManualCheckpointRequired,
    PreviousBatchNotFilled,
    PreviousBatchPartiallyFilled,
    PreviousBatchMissingOrders,
    ExecutionPausedAfterReject,
};

// ── TradeOrder -- strongly typed, all getters private ──
class TradeOrder final {
public:
    TradeOrder() = default;

    [[nodiscard]] const std::string& strategyId() const noexcept { return m_strategyId; }
    void setStrategyId(std::string v) { m_strategyId = std::move(v); }

    [[nodiscard]] const std::string& symbol() const noexcept { return m_symbol; }
    void setSymbol(std::string v) { m_symbol = std::move(v); }

    [[nodiscard]] strategy::OrderDirection side() const noexcept { return m_side; }
    void setSide(strategy::OrderDirection v) noexcept { m_side = v; }

    [[nodiscard]] strategy::PositionEffect positionEffect() const noexcept { return m_positionEffect; }
    void setPositionEffect(strategy::PositionEffect v) noexcept { m_positionEffect = v; }

    [[nodiscard]] double price() const noexcept { return m_price; }
    void setPrice(double v) noexcept { m_price = v; }

    [[nodiscard]] std::int64_t quantity() const noexcept { return m_quantity; }
    void setQuantity(std::int64_t v) noexcept { m_quantity = v; }

    [[nodiscard]] double signalStrength() const noexcept { return m_signalStrength; }
    void setSignalStrength(double v) noexcept { m_signalStrength = v; }

    [[nodiscard]] double cashAmount() const noexcept { return m_cashAmount; }
    void setCashAmount(double v) noexcept { m_cashAmount = v; }

    [[nodiscard]] ActionKind actionKind() const noexcept { return m_actionKind; }
    void setActionKind(ActionKind v) noexcept { m_actionKind = v; }

    [[nodiscard]] bool isBoardLotMode() const noexcept { return m_isBoardLotMode; }
    void setBoardLotMode(bool v) noexcept { m_isBoardLotMode = v; }

    // Execution batch metadata
    [[nodiscard]] const std::string& batchId() const noexcept { return m_batchId; }
    void setBatchId(std::string v) { m_batchId = std::move(v); }

    [[nodiscard]] int batchIndex() const noexcept { return m_batchIndex; }
    void setBatchIndex(int v) noexcept { m_batchIndex = v; }

    [[nodiscard]] const std::string& executionScopeId() const noexcept { return m_executionScopeId; }
    void setExecutionScopeId(std::string v) { m_executionScopeId = std::move(v); }

    [[nodiscard]] const std::string& previousBatchId() const noexcept { return m_previousBatchId; }
    void setPreviousBatchId(std::string v) { m_previousBatchId = std::move(v); }

    [[nodiscard]] int previousBatchOrderCount() const noexcept { return m_previousBatchOrderCount; }
    void setPreviousBatchOrderCount(int v) noexcept { m_previousBatchOrderCount = v; }

    [[nodiscard]] bool requiresPreviousBatchFilled() const noexcept { return m_requiresPreviousBatchFilled; }
    void setRequiresPreviousBatchFilled(bool v) noexcept { m_requiresPreviousBatchFilled = v; }

    [[nodiscard]] bool pauseOnConflict() const noexcept { return m_pauseOnConflict; }
    void setPauseOnConflict(bool v) noexcept { m_pauseOnConflict = v; }

    [[nodiscard]] bool pauseOnAbnormalReject() const noexcept { return m_pauseOnAbnormalReject; }
    void setPauseOnAbnormalReject(bool v) noexcept { m_pauseOnAbnormalReject = v; }

    [[nodiscard]] bool requiresManualCheckpoint() const noexcept { return m_requiresManualCheckpoint; }
    void setRequiresManualCheckpoint(bool v) noexcept { m_requiresManualCheckpoint = v; }

    [[nodiscard]] int manualCheckpointBatchIndex() const noexcept { return m_manualCheckpointBatchIndex; }
    void setManualCheckpointBatchIndex(int v) noexcept { m_manualCheckpointBatchIndex = v; }

    // ── 订单生命周期状态 ──
    [[nodiscard]] const std::string& brokerOrderId() const noexcept { return m_brokerOrderId; }
    void setBrokerOrderId(std::string v) { m_brokerOrderId = std::move(v); }

    [[nodiscard]] OrderStatusValue status() const noexcept { return m_status; }
    void setStatus(OrderStatusValue v) noexcept { m_status = v; }

    [[nodiscard]] double filledPrice() const noexcept { return m_filledPrice; }
    void setFilledPrice(double v) noexcept { m_filledPrice = v; }

    [[nodiscard]] std::int64_t filledQuantity() const noexcept { return m_filledQuantity; }
    void setFilledQuantity(std::int64_t v) noexcept { m_filledQuantity = v; }

    [[nodiscard]] const std::string& statusMessage() const noexcept { return m_statusMessage; }
    void setStatusMessage(std::string v) { m_statusMessage = std::move(v); }

    [[nodiscard]] bool isClosed() const noexcept {
        return m_status == OrderStatusValue::Filled
            || m_status == OrderStatusValue::Cancelled
            || m_status == OrderStatusValue::Rejected
            || m_status == OrderStatusValue::Expired;
    }

private:
    std::string m_strategyId;
    std::string m_symbol;
    strategy::OrderDirection m_side{strategy::OrderDirection::Buy};
    strategy::PositionEffect m_positionEffect{strategy::PositionEffect::Unspecified};
    double m_price{0.0};
    std::int64_t m_quantity{0};
    double m_signalStrength{1.0};
    double m_cashAmount{0.0};
    ActionKind m_actionKind{ActionKind::Normal};
    bool m_isBoardLotMode{true};

    std::string m_brokerOrderId;
    OrderStatusValue m_status{OrderStatusValue::Pending};
    double m_filledPrice{0.0};
    std::int64_t m_filledQuantity{0};
    std::string m_statusMessage;

    std::string m_batchId;
    int m_batchIndex{0};
    std::string m_executionScopeId;
    std::string m_previousBatchId;
    int m_previousBatchOrderCount{0};
    bool m_requiresPreviousBatchFilled{false};
    bool m_pauseOnConflict{false};
    bool m_pauseOnAbnormalReject{false};
    bool m_requiresManualCheckpoint{false};
    int m_manualCheckpointBatchIndex{-1};
};

// ── Results ──
class ValidationResult final {
public:
    static ValidationResult ok() { return ValidationResult(true, OrderValidationCode::Ok, "ok"); }
    static ValidationResult reject(OrderValidationCode code, std::string msg) {
        return ValidationResult(false, code, std::move(msg));
    }
    [[nodiscard]] bool valid() const noexcept { return m_valid; }
    [[nodiscard]] OrderValidationCode code() const noexcept { return m_code; }
    [[nodiscard]] const std::string& message() const noexcept { return m_message; }
private:
    ValidationResult(bool valid, OrderValidationCode code, std::string msg)
        : m_valid(valid), m_code(code), m_message(std::move(msg)) {}
    bool m_valid{true};
    OrderValidationCode m_code{OrderValidationCode::Ok};
    std::string m_message;
};

class SchedulingConflictResult final {
public:
    SchedulingConflictResult() = default;
    [[nodiscard]] bool hasConflict() const noexcept { return m_hasConflict; }
    void setConflict(ScheduleConflictCode code, std::string msg) {
        m_hasConflict = true; m_code = code; m_message = std::move(msg);
    }
    [[nodiscard]] ScheduleConflictCode code() const noexcept { return m_code; }
    [[nodiscard]] const std::string& message() const noexcept { return m_message; }
private:
    bool m_hasConflict{false};
    ScheduleConflictCode m_code{ScheduleConflictCode::None};
    std::string m_message;
};

class SubmitResult final {
public:
    static SubmitResult success(BrokerOrderId id) {
        SubmitResult r; r.m_succeeded = true; r.m_brokerOrderId = std::move(id); return r;
    }
    static SubmitResult rejected(std::string reason, OrderValidationCode code = OrderValidationCode::Ok) {
        SubmitResult r; r.m_succeeded = false; r.m_message = std::move(reason); r.m_validationCode = code; return r;
    }
    static SubmitResult scheduleBlocked(ScheduleConflictCode code, std::string msg) {
        SubmitResult r; r.m_succeeded = false; r.m_message = std::move(msg); r.m_scheduleCode = code; return r;
    }
    static SubmitResult riskRejected(strategy::RiskRejectCode code, std::string desc) {
        SubmitResult r; r.m_succeeded = false; r.m_message = std::move(desc); r.m_riskCode = code; return r;
    }
    [[nodiscard]] bool succeeded() const noexcept { return m_succeeded; }
    [[nodiscard]] const BrokerOrderId& brokerOrderId() const noexcept { return m_brokerOrderId; }
    [[nodiscard]] const std::string& message() const noexcept { return m_message; }
    [[nodiscard]] OrderValidationCode validationCode() const noexcept { return m_validationCode; }
    [[nodiscard]] ScheduleConflictCode scheduleCode() const noexcept { return m_scheduleCode; }
    [[nodiscard]] strategy::RiskRejectCode riskCode() const noexcept { return m_riskCode; }
private:
    SubmitResult() = default;
    bool m_succeeded{false};
    BrokerOrderId m_brokerOrderId;
    std::string m_message;
    OrderValidationCode m_validationCode{OrderValidationCode::Ok};
    ScheduleConflictCode m_scheduleCode{ScheduleConflictCode::None};
    strategy::RiskRejectCode m_riskCode{strategy::RiskRejectCode::None};
};

// ── Engine ──
class TradeExecutionEngine final {
public:
    TradeExecutionEngine();
    ~TradeExecutionEngine();

    // Dependency injection
    void setGateway(std::unique_ptr<IBrokerGateway> gateway);
    IBrokerGateway* gateway() const noexcept;

    // ── Order operations ──
    SubmitResult submitOrder(const TradeOrder& order,
                             const strategy::RiskInput& riskContext);
    bool cancelOrder(BrokerOrderId brokerOrderId);

    // ── Queries ──
    [[nodiscard]] const std::vector<TradeOrder>& recentOrders() const noexcept;

    // ── Scheduling controls ──
    bool approveExecutionCheckpoint(const std::string& executionScopeId,
                                    const std::string& batchId);
    bool resumeExecutionPause(const std::string& executionScopeId,
                              const std::string& pausedBatchId = "");

    // ── Static validation ──
    [[nodiscard]] static ValidationResult validateOrder(const TradeOrder& order);

    // ── Callbacks (for bridge layer to connect to EventBus) ──
    using OrderAcceptedCallback = std::function<void(const TradeOrder&)>;
    using OrderUpdateCallback = std::function<void(const TradeOrder&)>;
    using TradeFillCallback = std::function<void(const TradeFill&)>;

    /// @brief 订单通过验证/调度/风控后同步回调（bridge 层由此写入 recentOrders）
    void setOnOrderAccepted(OrderAcceptedCallback cb) noexcept;
    void setOnOrderUpdate(OrderUpdateCallback cb) noexcept;
    void setOnTradeFill(TradeFillCallback cb) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace domain::trading