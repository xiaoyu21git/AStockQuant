#pragma once

#include "../../../domain/trading/include/TradingTypes.h"

#include <QVariantList>
#include <QVariantMap>

namespace application::trading {

class LiveTradingAdapter {
public:
    virtual ~LiveTradingAdapter() = default;

    [[nodiscard]] virtual domain::trading::TradeIntentBatch buildIntentBatch(
        const QVariantMap& request) const = 0;

    [[nodiscard]] virtual domain::trading::TradingExecutionContext buildExecutionContext(
        const QVariantMap& tradingConfiguration,
        const QVariantMap& riskConfiguration) const = 0;

    [[nodiscard]] virtual std::vector<domain::trading::FillEvent> translateRuntimeFeedback(
        const QVariantList& runtimeEvents) const = 0;
};

} // namespace application::trading