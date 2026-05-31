#include "EarningsSurpriseStrategy.h"

#include <cmath>

namespace {

constexpr int kInvalidTradingDay = -1;
constexpr int kZeroDays = 0;
constexpr double kZeroValue = 0.0;
constexpr double kSurpriseEpsilon = 1e-12;

}

namespace domain::strategies {

bool EarningsSurpriseStrategy::isConfigured() const noexcept
{
    return IStrategy::isConfigured()
        && hasUsableParameters();
}

std::vector<EarningsSurpriseValue> EarningsSurpriseStrategy::computeSurpriseValues(
    const std::vector<EarningsEventSnapshot>& events) const
{
    if (!isConfigured() || events.empty()) {
        return {};
    }

    std::vector<EarningsSurpriseValue> values;
    values.reserve(events.size());
    for (const EarningsEventSnapshot& event : events) {
        if (event.symbolId == 0
            || event.tradingDay <= kInvalidTradingDay
            || !acceptsEventSource(event.eventSource)) {
            continue;
        }

        values.push_back(EarningsSurpriseValue{
            event.symbolId,
            computeSurprise(event),
            event.eventSource,
            event.tradingDay});
    }

    return values;
}

bool EarningsSurpriseStrategy::hasUsableParameters() const noexcept
{
    return spec_.surpriseThreshold > kZeroValue
        && spec_.holdDays > kZeroDays
        && !spec_.eventSources.empty();
}

bool EarningsSurpriseStrategy::acceptsEventSource(EventSourceKind eventSource) const noexcept
{
    for (const EventSourceKind configuredSource : spec_.eventSources) {
        if (configuredSource == eventSource) {
            return true;
        }
    }

    return false;
}

double EarningsSurpriseStrategy::computeSurprise(const EarningsEventSnapshot& event) noexcept
{
    const double denominator = std::abs(event.expectedValue) > kSurpriseEpsilon
        ? std::abs(event.expectedValue)
        : kSurpriseEpsilon;
    return (event.actualValue - event.expectedValue) / denominator;
}

} // namespace domain::strategies