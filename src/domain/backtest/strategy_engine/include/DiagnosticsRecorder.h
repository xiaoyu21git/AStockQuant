#pragma once

#include "StrategyBacktestEngineInterfaces.h"

namespace domain::backtest::strategy_engine {

class DiagnosticsRecorder final {
public:
    DiagnosticsRecorder() = default;

    void recordAssumption(const EngineAssumption& assumption);
    void recordValidationIssue(const ValidationIssue& validationIssue);
    void recordDiagnostic(const DiagnosticRecord& diagnosticRecord);
    void markElapsed(DurationNs elapsed);
    void markPeakMemory(MemoryBytes peakMemory);

    [[nodiscard]] Diagnostics snapshot() const;

private:
    Diagnostics diagnostics_;
};

} // namespace domain::backtest::strategy_engine