#pragma once

#include "IFactorSignalSetAssembler.h"

namespace factor::compute {

struct SignalTensorBuffer final {
    std::vector<double> values;
    std::vector<uint8_t> mask;
    int32_t timeCount{0};
    int32_t instrumentCount{0};
    int32_t factorCount{0};

    [[nodiscard]] bool isValid() const noexcept
    {
        if (timeCount <= 0 || instrumentCount <= 0 || factorCount <= 0) {
            return false;
        }

        const size_t expectedCount = static_cast<size_t>(timeCount)
            * static_cast<size_t>(instrumentCount)
            * static_cast<size_t>(factorCount);
        return values.size() == expectedCount && mask.size() == expectedCount;
    }

    [[nodiscard]] ProcessedTensorView asView() const noexcept
    {
        ProcessedTensorView tensorView;
        tensorView.values = values.data();
        tensorView.mask = mask.data();
        tensorView.timeCount = timeCount;
        tensorView.instrumentCount = instrumentCount;
        tensorView.factorCount = factorCount;
        return tensorView;
    }
};

class IPostProcessingStep {
public:
    virtual ~IPostProcessingStep() = default;

    virtual void
    apply(SignalTensorBuffer& tensor, const PostProcessingConfig& config) const = 0;
};

class IPostProcessingPipeline {
public:
    virtual ~IPostProcessingPipeline() = default;

    [[nodiscard]] virtual FactorResult<SignalTensorBuffer>
    run(SignalTensorBuffer rawTensor, const GenerateSpec& spec) const = 0;
};

} // namespace factor::compute
