#include "DiagnosticsRecorder.h"

namespace domain::backtest::strategy_engine {

void DiagnosticsRecorder::recordAssumption(const EngineAssumption& assumption)
{
    if (assumption.isValid()) {
        diagnostics_.assumptions.add(assumption);
    }
}

void DiagnosticsRecorder::recordValidationIssue(const ValidationIssue& validationIssue)
{
    if (validationIssue.isValid()) {
        diagnostics_.validationIssues.add(validationIssue);
    }
}

void DiagnosticsRecorder::recordDiagnostic(const DiagnosticRecord& diagnosticRecord)
{
    if (diagnosticRecord.isValid()) {
        diagnostics_.records.add(diagnosticRecord);
    }
}

void DiagnosticsRecorder::markElapsed(DurationNs elapsed)
{
    diagnostics_.elapsed = elapsed;
}

void DiagnosticsRecorder::markPeakMemory(MemoryBytes peakMemory)
{
    diagnostics_.peakMemory = peakMemory;
}

Diagnostics DiagnosticsRecorder::snapshot() const
{
    return diagnostics_;
}

} // namespace domain::backtest::strategy_engine