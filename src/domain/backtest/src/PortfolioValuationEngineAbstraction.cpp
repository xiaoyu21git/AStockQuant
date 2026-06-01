#include "PortfolioValuationEngineAbstraction.h"

#include <limits>
#include <unordered_set>
#include <unordered_map>
#include <utility>

namespace astock::domain::backtest::portfolio_valuation {

bool MarkToMarketValuationEngine::checkedMultiplyInt64(int64_t left, int64_t right, int64_t* out)
{
    if (out == nullptr) {
        return false;
    }

    if (left == 0 || right == 0) {
        *out = 0;
        return true;
    }

    if (left > std::numeric_limits<int64_t>::max() / right) {
        return false;
    }

    *out = left * right;
    return true;
}

bool MarkToMarketValuationEngine::checkedAddInt64(int64_t left, int64_t right, int64_t* out)
{
    if (out == nullptr) {
        return false;
    }

    if (right > 0 && left > std::numeric_limits<int64_t>::max() - right) {
        return false;
    }

    *out = left + right;
    return true;
}

PortfolioValuationResult MarkToMarketValuationEngine::evaluate(ValuationSpec spec,
                                                               std::vector<HoldingState> holdings,
                                                               std::vector<MarketQuote> quotes) const
{
    if (!spec.isValid()) {
        return PortfolioValuationResult{PortfolioValuationError::InvalidInput, std::nullopt};
    }

    std::unordered_map<uint32_t, int32_t> quoteByInstrument;
    quoteByInstrument.reserve(quotes.size());
    for (const MarketQuote& quote : quotes) {
        if (!quote.isValid()) {
            return PortfolioValuationResult{PortfolioValuationError::InvalidQuote, std::nullopt};
        }
        const auto inserted = quoteByInstrument.emplace(quote.instrument.value, quote.lastPrice.value);
        if (!inserted.second) {
            return PortfolioValuationResult{PortfolioValuationError::InvalidQuote, std::nullopt};
        }
    }

    PortfolioValuationSummary summary;
    std::unordered_set<uint32_t> seenHoldings;
    seenHoldings.reserve(holdings.size());
    for (const HoldingState& holding : holdings) {
        if (!holding.isValid()) {
            return PortfolioValuationResult{PortfolioValuationError::InvalidHolding, std::nullopt};
        }
        if (!seenHoldings.insert(holding.instrument.value).second) {
            return PortfolioValuationResult{PortfolioValuationError::InvalidHolding, std::nullopt};
        }

        const auto it = quoteByInstrument.find(holding.instrument.value);
        if (it == quoteByInstrument.end()) {
            return PortfolioValuationResult{PortfolioValuationError::MissingQuote, std::nullopt};
        }

        int64_t priceMicros = 0;
        if (!MarkToMarketValuationEngine::checkedMultiplyInt64(static_cast<int64_t>(it->second), spec.microsPerTick, &priceMicros)) {
            return PortfolioValuationResult{PortfolioValuationError::InvalidInput, std::nullopt};
        }

        int64_t longMicros = 0;
        if (!MarkToMarketValuationEngine::checkedMultiplyInt64(priceMicros, static_cast<int64_t>(holding.longLots.value), &longMicros)) {
            return PortfolioValuationResult{PortfolioValuationError::InvalidInput, std::nullopt};
        }

        int64_t shortMicros = 0;
        if (!MarkToMarketValuationEngine::checkedMultiplyInt64(priceMicros, static_cast<int64_t>(holding.shortLots.value), &shortMicros)) {
            return PortfolioValuationResult{PortfolioValuationError::InvalidInput, std::nullopt};
        }

        if (!MarkToMarketValuationEngine::checkedAddInt64(summary.grossLongMicros, longMicros, &summary.grossLongMicros)) {
            return PortfolioValuationResult{PortfolioValuationError::InvalidInput, std::nullopt};
        }
        if (!MarkToMarketValuationEngine::checkedAddInt64(summary.grossShortMicros, shortMicros, &summary.grossShortMicros)) {
            return PortfolioValuationResult{PortfolioValuationError::InvalidInput, std::nullopt};
        }
    }

    summary.netMicros = summary.grossLongMicros - summary.grossShortMicros;
    return PortfolioValuationResult{PortfolioValuationError::None, std::move(summary)};
}

} // namespace astock::domain::backtest::portfolio_valuation
