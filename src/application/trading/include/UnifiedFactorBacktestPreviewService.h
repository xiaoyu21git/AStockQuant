#pragma once

#include "../../../domain/strategy/include/StrategySnapshotTypes.h"

namespace domain::backtest {
class StockDataProvider;
}

namespace factor {
struct BacktestResult;
}

namespace application::trading {

struct FactorBacktestTradingPreviewSummary final {
    enum class Status : int {
        InvalidBacktestResult = 0,
        MissingFactorSnapshot = 1,
        InvalidBatch = 2,
        InvalidContext = 3,
        Pass = 4,
        Warn = 5,
        Blocked = 6,
        ForceReduce = 7,
        TradingHalt = 8,
        NoOrderPlan = 9,
    };

    Status status{Status::InvalidBacktestResult};
    QString message;
    domain::strategy::ReasonCode riskReasonCode;
    domain::strategy::DiagnosticCode diagnosticCode{domain::strategy::DiagnosticCode::None};
    int targetPositionCount{0};
    int orderPlanCount{0};
    int acceptedOrderCount{0};
    int fillCount{0};
};

class UnifiedFactorBacktestPreviewService final {
public:
    [[nodiscard]] FactorBacktestTradingPreviewSummary preview(
        const factor::BacktestResult& result,
        domain::backtest::StockDataProvider* stockDataProvider = nullptr) const;
};

} // namespace application::trading