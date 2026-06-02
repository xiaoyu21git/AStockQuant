#pragma once

#include "BacktestInterfaces.hpp"

namespace application::backtest {

class RequestedTargetPositionConstructionAdapter final : public IPortfolioConstructionEngine {
public:
    [[nodiscard]] StageResult constructTargetPosition(RunContext& context) const override;

private:
    static constexpr std::uint32_t kMinimumSignalCount = 1U;
    static constexpr int kMinimumRequestedTargetCount = 1;
};

} // namespace application::backtest