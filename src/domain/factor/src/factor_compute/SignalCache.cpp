#include "factor_compute/SignalCache.h"

#include <cstring>

namespace factor::compute {

namespace {

template <typename TValue>
size_t mixValue(size_t seed, TValue value)
{
    constexpr size_t kPrime = 1099511628211ULL;
    seed ^= static_cast<size_t>(value);
    seed *= kPrime;
    return seed;
}

uint64_t bitwiseDouble(double value)
{
    uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(double));
    return bits;
}

size_t hashDateRange(const DateRange& dateRange)
{
    constexpr size_t kOffsetBasis = 1469598103934665603ULL;
    size_t seed = kOffsetBasis;
    seed = mixValue(seed, dateRange.from.value);
    seed = mixValue(seed, dateRange.to.value);
    return seed;
}

size_t hashPostProcessingConfig(size_t seed, const PostProcessingConfig& config)
{
    seed = mixValue(seed, bitwiseDouble(config.winsorizeStdBand));
    seed = mixValue(seed, bitwiseDouble(config.stdEpsilon));
    seed = mixValue(seed, config.minimumValidSampleCount);
    return seed;
}

template <typename TValue, typename TExtractor>
size_t hashVectorValues(size_t seed, const std::vector<TValue>& values, TExtractor extractor)
{
    seed = mixValue(seed, values.size());
    for (const TValue& value : values) {
        seed = mixValue(seed, extractor(value));
    }
    return seed;
}

bool equalsDateRange(const DateRange& lhs, const DateRange& rhs)
{
    return lhs.from.value == rhs.from.value && lhs.to.value == rhs.to.value;
}

bool equalsPostProcessingConfig(const PostProcessingConfig& lhs, const PostProcessingConfig& rhs)
{
    return bitwiseDouble(lhs.winsorizeStdBand) == bitwiseDouble(rhs.winsorizeStdBand)
        && bitwiseDouble(lhs.stdEpsilon) == bitwiseDouble(rhs.stdEpsilon)
        && lhs.minimumValidSampleCount == rhs.minimumValidSampleCount;
}

template <typename TValue, typename TExtractor>
bool equalsVectorValues(const std::vector<TValue>& lhs, const std::vector<TValue>& rhs, TExtractor extractor)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (size_t index = 0; index < lhs.size(); ++index) {
        if (extractor(lhs[index]) != extractor(rhs[index])) {
            return false;
        }
    }
    return true;
}

} // namespace

size_t SignalCache::SignalCacheKeyHash::operator()(const SignalCacheKey& key) const noexcept
{
    size_t seed = hashDateRange(key.dateRange);
    seed = mixValue(seed, static_cast<uint8_t>(key.mode));
    seed = hashPostProcessingConfig(seed, key.postProcessingConfig);
    seed = hashVectorValues(seed, key.instruments, [](const InstrumentId id) { return id.value; });
    seed = hashVectorValues(seed, key.factors, [](const FactorId id) { return id.value; });
    return seed;
}

bool SignalCache::SignalCacheKeyEqual::operator()(const SignalCacheKey& lhs, const SignalCacheKey& rhs) const noexcept
{
    return static_cast<uint8_t>(lhs.mode) == static_cast<uint8_t>(rhs.mode)
        && equalsDateRange(lhs.dateRange, rhs.dateRange)
        && equalsPostProcessingConfig(lhs.postProcessingConfig, rhs.postProcessingConfig)
        && equalsVectorValues(lhs.instruments, rhs.instruments, [](const InstrumentId id) { return id.value; })
        && equalsVectorValues(lhs.factors, rhs.factors, [](const FactorId id) { return id.value; });
}

std::optional<SignalSet> SignalCache::load(const SignalCacheKey& key) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iter = storage_.find(key);
    if (iter == storage_.end()) {
        return std::nullopt;
    }
    return iter->second;
}

void SignalCache::store(const SignalCacheKey& key, const SignalSet& signalSet)
{
    std::lock_guard<std::mutex> lock(mutex_);
    storage_[key] = signalSet;
}

} // namespace factor::compute
