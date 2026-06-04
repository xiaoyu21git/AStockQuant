#include "factor_compute/SignalSetBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace factor::compute {

SignalSet SignalSetBuilder::build(
    const FactorValuesByDate& factorValues,
    const std::vector<DateKey>& dates,
    const std::vector<std::string>& dateStrs,
    const std::vector<InstrumentId>& instruments)
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
    signalSet.mask.assign(flatSize, 1U);  // 1=缺失, 0=存在
    signalSet.index = {instCount, 1, 1};   // timeStride=instCount, instrumentStride=1, factorStride=1
    signalSet.progress = {1, 1};

    for (int32_t di = 0; di < timeCount; ++di) {
        const std::string& dateStr = dateStrs[di];
        auto dateIt = factorValues.find(dateStr);
        if (dateIt == factorValues.end()) continue;

        for (int32_t ii = 0; ii < instCount; ++ii) {
            std::string symStr = std::to_string(instruments[ii].value);
            auto valIt = dateIt->second.find(symStr);
            if (valIt != dateIt->second.end() && std::isfinite(valIt->second)) {
                signalSet.values[di * instCount + ii] = valIt->second;
                signalSet.mask[di * instCount + ii] = 0U;  // 0=存在
            }
        }
    }

    return signalSet;
}

} // namespace factor::compute