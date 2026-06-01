#include "SignalOrderTranslatorAbstraction.h"

#include <algorithm>
#include <utility>

namespace astock::domain::backtest::signal_orders {

TranslationResult LinearSignalOrderTranslator::translate(TranslationSpec spec,
                                                         std::vector<SignalSnapshot> signalSnapshots) const
{
    if (!spec.isValid()) {
        return TranslationResult{TranslationError::InvalidInput, std::nullopt};
    }

    std::sort(signalSnapshots.begin(), signalSnapshots.end(),
              [](const SignalSnapshot& left, const SignalSnapshot& right) {
                  if (left.signal.value != right.signal.value) {
                      return left.signal.value > right.signal.value;
                  }
                  return left.instrument.value < right.instrument.value;
              });

    std::unordered_set<uint32_t> seen;
    seen.reserve(signalSnapshots.size());

    OrderInstructionSet out;
    out.items.reserve(signalSnapshots.size());

    for (const SignalSnapshot& snapshot : signalSnapshots) {
        if (!snapshot.isValid()) {
            return TranslationResult{TranslationError::InvalidSignal, std::nullopt};
        }
        const auto inserted = seen.insert(snapshot.instrument.value);
        if (!inserted.second) {
            return TranslationResult{TranslationError::DuplicateInstrument, std::nullopt};
        }
        if (snapshot.signal.value == 0) {
            continue;
        }

        if (snapshot.signal.value > 0) {
            const int32_t raw = snapshot.signal.value;
            const int32_t delta = std::min(raw, spec.maxBuyDelta.value);
            out.items.push_back(OrderInstruction{snapshot.instrument, OrderAction::Buy, WeightDeltaBps{delta}});
            continue;
        }

        const int32_t raw = -snapshot.signal.value;
        const int32_t delta = std::min(raw, spec.maxSellDelta.value);
        out.items.push_back(OrderInstruction{snapshot.instrument, OrderAction::Sell, WeightDeltaBps{delta}});
    }

    return TranslationResult{TranslationError::None, std::move(out)};
}

} // namespace astock::domain::backtest::signal_orders
