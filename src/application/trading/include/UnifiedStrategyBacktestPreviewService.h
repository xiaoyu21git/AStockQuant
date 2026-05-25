#pragma once

#include "../../../domain/backtest/include/BacktestRequest.h"

namespace domain::backtest {
class StockDataProvider;
}

namespace application::trading {

struct StrategyBacktestTradingPreviewSummary final {
    enum class Status : int {
        InvalidRequest = 0,
        InvalidBatch = 1,
        InvalidContext = 2,
        Pass = 3,
        Warn = 4,
        Blocked = 5,
        ForceReduce = 6,
        TradingHalt = 7,
        NoOrderPlan = 8,
    };

    Status status{Status::InvalidRequest};
    QString message;
    domain::strategy::ReasonCode riskReasonCode;
    domain::strategy::DiagnosticCode diagnosticCode{domain::strategy::DiagnosticCode::None};
    int intentCount{0};
    int targetPositionCount{0};
    int orderPlanCount{0};
    int acceptedOrderCount{0};
    int fillCount{0};
};

class UnifiedStrategyBacktestPreviewService final {
public:
    [[nodiscard]] StrategyBacktestTradingPreviewSummary preview(
    const domain::backtest::BacktestRequest& request,
    domain::backtest::StockDataProvider* stockDataProvider = nullptr) const;
};

} // namespace application::trading