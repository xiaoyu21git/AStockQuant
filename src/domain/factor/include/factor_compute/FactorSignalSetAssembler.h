#pragma once

#include "IFactorSignalSetAssembler.h"

namespace factor::compute {

class FactorSignalSetAssembler final : public IFactorSignalSetAssembler {
public:
    [[nodiscard]] SignalSet assemble(
        const ProcessedTensorView& tensor,
        const AssembleContext& context) const override;
};

} // namespace factor::compute

