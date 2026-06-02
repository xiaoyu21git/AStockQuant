#pragma once

#include "FactorSignalTypes.h"
#include "IFactorOperatorLibrary.h"

namespace factor::compute {

class IAnalysisModule {
public:
    virtual ~IAnalysisModule() = default;

    [[nodiscard]] virtual FactorResult<AnalysisReport>
    analyze(const SignalSet& signalSet, const GenerateSpec& spec, NumericConstMatrixView closeView) const = 0;
};

class AnalysisModule final : public IAnalysisModule {
public:
    [[nodiscard]] FactorResult<AnalysisReport>
    analyze(const SignalSet& signalSet, const GenerateSpec& spec, NumericConstMatrixView closeView) const override;
};

} // namespace factor::compute
