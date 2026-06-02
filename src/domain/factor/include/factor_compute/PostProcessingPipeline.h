#pragma once

#include "IPostProcessingPipeline.h"

#include <memory>
#include <vector>

namespace factor::compute {

class IPostProcessingStepFactory {
public:
    virtual ~IPostProcessingStepFactory() = default;

    [[nodiscard]] virtual std::vector<std::unique_ptr<const IPostProcessingStep>>
    createDefaultSteps() const = 0;
};

class DefaultPostProcessingStepFactory final : public IPostProcessingStepFactory {
public:
    [[nodiscard]] std::vector<std::unique_ptr<const IPostProcessingStep>>
    createDefaultSteps() const override;
};

class PostProcessingPipeline final : public IPostProcessingPipeline {
public:
    PostProcessingPipeline();

    explicit PostProcessingPipeline(const IPostProcessingStepFactory& stepFactory) noexcept;

    explicit PostProcessingPipeline(std::vector<std::unique_ptr<const IPostProcessingStep>> steps) noexcept;

    [[nodiscard]] FactorResult<SignalTensorBuffer>
    run(SignalTensorBuffer rawTensor, const GenerateSpec& spec) const override;

private:
    std::vector<std::unique_ptr<const IPostProcessingStep>> steps_;
};

} // namespace factor::compute
