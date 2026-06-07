#pragma once

#include "FactorSignalTypes.h"

namespace factor::compute {

struct AssembleContext final {
    std::vector<DateKey> dates;
    std::vector<InstrumentId> instruments;
    std::vector<FactorId> factors;
    SignalProgress progress{};
    bool isPartial{false};

    [[nodiscard]] bool isValid() const noexcept
    {
        return !dates.empty()
            && !instruments.empty()
            && !factors.empty()
            && progress.isValid()
            && progress.plannedFactorCount == static_cast<uint32_t>(factors.size());
    }
};

struct ProcessedTensorView final {
    const signal_value_t* values{nullptr};
    const uint8_t* mask{nullptr};
    int32_t timeCount{0};
    int32_t instrumentCount{0};
    int32_t factorCount{0};

    [[nodiscard]] bool isValid() const noexcept
    {
        return values != nullptr
            && mask != nullptr
            && timeCount > 0
            && instrumentCount > 0
            && factorCount > 0;
    }
};

class IFactorSignalSetAssembler {
public:
    virtual ~IFactorSignalSetAssembler() = default;

    [[nodiscard]] virtual SignalSet assemble(
        const ProcessedTensorView& tensor,
        const AssembleContext& context) const = 0;
};

} // namespace factor::compute

