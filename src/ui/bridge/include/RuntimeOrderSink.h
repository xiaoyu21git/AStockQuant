#pragma once

#include "../../domain/strategy/include/IStrategyService.h"

namespace domain::trading {
class TradeExecutionEngine;
}

namespace bridge {

class RuntimeOrderSink final : public domain::strategy::IRuntimeOrderSink {
public:
    explicit RuntimeOrderSink(domain::trading::TradeExecutionEngine& engine);

    [[nodiscard]] domain::strategy::StrategyServiceFlowResult submit(
        const domain::strategy::OrderRequest& order) override;

private:
    domain::trading::TradeExecutionEngine& engine_;
};

} // namespace bridge
