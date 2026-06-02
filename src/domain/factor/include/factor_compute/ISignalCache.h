#pragma once

#include "FactorSignalTypes.h"

#include <optional>

namespace factor::compute {

struct SignalCacheKey final {
    SignalEngineMode mode{SignalEngineMode::FullPipeline};
    DateRange dateRange{};
    PostProcessingConfig postProcessingConfig{};
    std::vector<InstrumentId> instruments;
    std::vector<FactorId> factors;

    [[nodiscard]] bool isValid() const noexcept
    {
        return dateRange.isValid()
            && postProcessingConfig.isValid()
            && !instruments.empty()
            && !factors.empty();
    }
};

class ISignalCache {
public:
    virtual ~ISignalCache() = default;

    [[nodiscard]] virtual std::optional<SignalSet>
    load(const SignalCacheKey& key) const = 0;

    virtual void
    store(const SignalCacheKey& key, const SignalSet& signalSet) = 0;
};

} // namespace factor::compute
