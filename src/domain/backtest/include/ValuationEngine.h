#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

#include "PortfolioValuationTypes.h"

namespace astock::domain::backtest::portfolio_valuation {

enum class PortfolioValuationError {
    None,
    InvalidInput,
    InvalidHolding,
    InvalidQuote,
    MissingQuote
};

struct PortfolioValuationResult final {
    PortfolioValuationError error{PortfolioValuationError::None};
    std::optional<PortfolioValuationSummary> value;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == PortfolioValuationError::None && value.has_value();
    }
};

class IPortfolioValuationEngine {
public:
    virtual ~IPortfolioValuationEngine() = default;

    virtual PortfolioValuationResult evaluate(ValuationSpec spec,
                                              std::vector<HoldingState> holdings,
                                              std::vector<MarketQuote> quotes) const = 0;
};

class MarkToMarketValuationEngine final : public IPortfolioValuationEngine {
public:
    PortfolioValuationResult evaluate(ValuationSpec spec,
                                      std::vector<HoldingState> holdings,
                                      std::vector<MarketQuote> quotes) const override;

private:
    static bool checkedMultiplyInt64(int64_t left, int64_t right, int64_t* out);
    static bool checkedAddInt64(int64_t left, int64_t right, int64_t* out);
};

} // namespace astock::domain::backtest::portfolio_valuation
