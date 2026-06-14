#pragma once

#include "TradingEnums.h"

#include <cstdint>
#include <string>

namespace domain::trading {

struct TradeOrder {
    std::string strategyId;
    std::string symbol;
    OrderSide side = OrderSide::Buy;
    double price = 0.0;
    int64_t quantity = 0;
    double cashAmount = 0.0;
    double signalStrength = 1.0;
    std::string batchId;
    int batchIndex = 0;
    std::string executionScopeId;
    std::string previousBatchId;
    OrderType orderType = OrderType::Limit;
};

struct RiskInput {
    std::string strategyId;
    std::string symbol;
    OrderSide side = OrderSide::Buy;
    double price = 0.0;
    int64_t quantity = 0;
    double signalStrength = 1.0;

    static RiskInput fromTradeOrder(const TradeOrder& order) {
        return {order.strategyId, order.symbol, order.side, order.price, order.quantity, order.signalStrength};
    }
};

struct OrderSubmissionRequest {
    std::string symbol;
    OrderSide side = OrderSide::Buy;
    double price = 0.0;
    int64_t quantity = 0;
    std::string orderType;   // "LIMIT" or "MARKET"
    std::string strategyId;
    std::string strategyName;
    std::string runtimeStrategyId;
    double cashAmount = 0.0;
    double signalStrength = 1.0;
    std::string batchId;
    int batchIndex = 0;
    std::string executionScopeId;
    std::string previousBatchId;
    std::string positionEffect;
    std::string action;
    std::string mode;
    std::string riskActionSource;
    std::string tradingDate;
    bool riskBypassTradingHalt = false;
    bool requiresPreviousBatchFilled = false;
    bool requiresManualCheckpoint = false;
    bool pauseOnAbnormalReject = false;
};

struct OrderSubmissionResult {
    bool accepted = false;
    ExecutionDecision decision = ExecutionDecision::Allow;
    std::string message;
};

} // namespace domain::trading