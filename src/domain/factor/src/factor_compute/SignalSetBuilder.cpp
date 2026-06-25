#include "factor_compute/SignalSetBuilder.h"

#include "foundation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace factor::compute {

SignalSet SignalSetBuilder::build(
    const FactorValuesByDate& factorValues,
    const std::vector<DateKey>& dates,
    const std::vector<std::string>& dateStrs,
    const std::vector<InstrumentId>& instruments,
    SignalEngineMode mode)
{
    SignalSet signalSet;
    signalSet.dates = dates;
    signalSet.instruments = instruments;
    signalSet.signalIds.push_back({1});
    signalSet.isPartial = false;

    const int32_t timeCount = static_cast<int32_t>(dateStrs.size());
    const int32_t instCount = static_cast<int32_t>(instruments.size());
    const int32_t flatSize = timeCount * instCount;

    signalSet.values.assign(flatSize, 0.0);
    signalSet.mask.assign(flatSize, 1U);
    signalSet.index = {instCount, 1, 1};
    signalSet.progress = {1, 1};

    if (timeCount <= 0 || instCount <= 0) {
        INTERNAL_WARN_STREAM << "SignalSetBuilder: empty dimensions, timeCount=" << timeCount << " instCount=" << instCount;
        return signalSet;
    }

    int32_t matchedDates = 0;
    int32_t totalMatchedValues = 0;

    for (int32_t di = 0; di < timeCount; ++di) {
        const std::string& dateStr = dateStrs[di];
        auto dateIt = factorValues.find(dateStr);
        if (dateIt == factorValues.end()) continue;
        ++matchedDates;

        int32_t dateMatched = 0;
        for (int32_t ii = 0; ii < instCount; ++ii) {
            std::string symStr = std::to_string(instruments[ii].value);
            auto valIt = dateIt->second.find(symStr);
            if (valIt != dateIt->second.end() && std::isfinite(valIt->second)) {
                signalSet.values[di * instCount + ii] = valIt->second;
                signalSet.mask[di * instCount + ii] = 0U;
                ++dateMatched;
            }
        }

        totalMatchedValues += dateMatched;
    }

    switch (mode) {
    case SignalEngineMode::SignalOnly:
        INTERNAL_INFO_STREAM << "SignalSetBuilder[SignalOnly] totalDates=" << timeCount << " matchedDates=" << matchedDates << " instruments=" << instCount << " matchedValues=" << totalMatchedValues;
        break;

    case SignalEngineMode::FullPipeline:
        INTERNAL_INFO_STREAM << "SignalSetBuilder[FullPipeline] totalDates=" << timeCount << " matchedDates=" << matchedDates << " instruments=" << instCount << " matchedValues=" << totalMatchedValues;
        break;

    case SignalEngineMode::Incremental:
        if (matchedDates == 0) {
            INTERNAL_WARN_STREAM << "SignalSetBuilder[Incremental] no valid signal for current date";
        }
        break;
    }

    if (matchedDates == 0 && factorValues.size() > 0) {
        INTERNAL_WARN_STREAM << "SignalSetBuilder: date format mismatch! dateStrs[0]=" << dateStrs[0] << " fvKey0=" << factorValues.begin()->first;
    }

    return signalSet;
}

} // namespace factor::compute