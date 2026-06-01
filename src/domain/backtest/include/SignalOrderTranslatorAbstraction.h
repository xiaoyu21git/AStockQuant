#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

#include "SignalOrderTypes.h"

namespace astock::domain::backtest::signal_orders {

struct TranslationSpec final {
    WeightDeltaBps maxBuyDelta{};
    WeightDeltaBps maxSellDelta{};

    [[nodiscard]] bool isValid() const noexcept
    {
        return maxBuyDelta.isValid() && maxSellDelta.isValid()
            && maxBuyDelta.value > WeightDeltaBps::kMinValue
            && maxSellDelta.value > WeightDeltaBps::kMinValue;
    }
};

enum class TranslationError {
    None,
    InvalidInput,
    InvalidSignal,
    DuplicateInstrument
};

struct TranslationResult final {
    TranslationError error{TranslationError::None};
    std::optional<OrderInstructionSet> value;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == TranslationError::None && value.has_value();
    }
};

class ISignalOrderTranslator {
public:
    virtual ~ISignalOrderTranslator() = default;

    virtual TranslationResult translate(TranslationSpec spec,
                                        std::vector<SignalSnapshot> signalSnapshots) const = 0;
};

class LinearSignalOrderTranslator final : public ISignalOrderTranslator {
public:
    TranslationResult translate(TranslationSpec spec,
                                std::vector<SignalSnapshot> signalSnapshots) const override;
};

} // namespace astock::domain::backtest::signal_orders
