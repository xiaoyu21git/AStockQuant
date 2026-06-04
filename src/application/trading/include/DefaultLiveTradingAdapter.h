#pragma once

#include "LiveTradingAdapter.h"

namespace application::trading {

class DefaultLiveTradingAdapter final : public LiveTradingAdapter {
public:
    [[nodiscard]] domain::trading::TradeIntentBatch buildIntentBatch(
        const QVariantMap& request) const override;

    [[nodiscard]] domain::trading::TradingExecutionContext buildExecutionContext(
        const QVariantMap& tradingConfiguration,
        const QVariantMap& riskConfiguration) const override;

    [[nodiscard]] std::vector<domain::trading::FillEvent> translateRuntimeFeedback(
        const QVariantList& runtimeEvents) const override;
};

} // namespace application::trading