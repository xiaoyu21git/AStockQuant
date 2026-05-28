#pragma once

#include <optional>
#include <stdexcept>
#include <utility>

#include "StrategyBacktestEngineTypes.h"

namespace domain::backtest::strategy_engine {

enum class EngineFailureCode : std::uint8_t {
    InvalidOpenTradeState = 0,
    MissingClosePrice = 1,
    MissingExecutionOrder = 2,
    InsufficientCash = 3,
    MissingPositionForSell = 4,
    UnsupportedUniverseMode = 5,
    InvalidRequest = 6,
    InvalidMarketDataSlice = 7,
    InvalidBenchmarkSlice = 8,
    InvalidBenchmarkStartPrice = 9,
};

[[nodiscard]] constexpr ValidationIssueCode engineFailureValidationIssueCode(const EngineFailureCode code) noexcept
{
    switch (code) {
    case EngineFailureCode::UnsupportedUniverseMode:
        return ValidationIssueCode::InvalidUniverse;
    case EngineFailureCode::InvalidRequest:
        return ValidationIssueCode::None;
    case EngineFailureCode::InvalidMarketDataSlice:
    case EngineFailureCode::InvalidBenchmarkSlice:
    case EngineFailureCode::InvalidBenchmarkStartPrice:
    case EngineFailureCode::MissingClosePrice:
        return ValidationIssueCode::InvalidDataSource;
    case EngineFailureCode::MissingExecutionOrder:
    case EngineFailureCode::InsufficientCash:
    case EngineFailureCode::MissingPositionForSell:
    case EngineFailureCode::InvalidOpenTradeState:
        return ValidationIssueCode::InvalidExecution;
    }

    return ValidationIssueCode::None;
}

[[nodiscard]] constexpr std::optional<DiagnosticRecord> engineFailureDiagnosticRecord(
    const EngineFailureCode code,
    const TradingDayIndex tradingDay = TradingDayIndex(),
    const LayerId layerId = LayerId(),
    const SymbolId symbolId = SymbolId()) noexcept
{
    const ValidationIssueCode validationCode = engineFailureValidationIssueCode(code);
    if (validationCode == ValidationIssueCode::None) {
        return std::nullopt;
    }

    return DiagnosticRecord{DiagnosticSeverity::Error,
                            EngineAssumptionCode::None,
                            validationCode,
                            tradingDay,
                            layerId,
                            symbolId};
}

class EngineFailure final {
public:
    explicit EngineFailure(EngineFailureCode code,
                           Diagnostics diagnostics = Diagnostics())
        : code_(code)
        , diagnostics_(std::move(diagnostics))
    {
        if (!diagnostics_.isValid()) {
            throw std::invalid_argument("EngineFailure requires valid diagnostics");
        }
    }

    [[nodiscard]] EngineFailureCode code() const noexcept
    {
        return code_;
    }

    [[nodiscard]] const Diagnostics& diagnostics() const noexcept
    {
        return diagnostics_;
    }

private:
    EngineFailureCode code_;
    Diagnostics diagnostics_;
};

} // namespace domain::backtest::strategy_engine