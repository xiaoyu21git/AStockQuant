#include "factor_compute/SignalCache.h"

#include <algorithm>
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

/// @brief 设计文档 Section 5.5 键规范化约束：
/// InstrumentSet 和 FactorSet 在构造键前必须按稳定顺序排序并去重。
/// 语义等价但输入顺序不同的请求必须落到同一缓存键。
template <typename TValue, typename TExtractor>
std::vector<TValue> canonicalizeVector(const std::vector<TValue>& values, TExtractor extractor)
{
    if (values.size() <= 1U) {
        return values;
    }

    std::vector<TValue> canonical = values;
    std::sort(canonical.begin(), canonical.end(),
        [&extractor](const TValue& lhs, const TValue& rhs) {
            return extractor(lhs) < extractor(rhs);
        });

    const auto uniqueEnd = std::unique(canonical.begin(), canonical.end(),
        [&extractor](const TValue& lhs, const TValue& rhs) {
            return extractor(lhs) == extractor(rhs);
        });
    canonical.erase(uniqueEnd, canonical.end());
    return canonical;
}

} // namespace

size_t SignalCache::SignalCacheKeyHash::operator()(const SignalCacheKey& key) const noexcept
{
    // 先规范化，再哈希，确保等价键落入同一桶
    const SignalCacheKey canonical = canonicalizeKey(key);

    size_t seed = hashDateRange(canonical.dateRange);
    seed = mixValue(seed, static_cast<uint8_t>(canonical.mode));
    seed = hashPostProcessingConfig(seed, canonical.postProcessingConfig);
    seed = hashVectorValues(seed, canonical.instruments, [](const InstrumentId id) { return id.value; });
    seed = hashVectorValues(seed, canonical.factors, [](const FactorId id) { return id.value; });
    return seed;
}

bool SignalCache::SignalCacheKeyEqual::operator()(const SignalCacheKey& lhs, const SignalCacheKey& rhs) const noexcept
{
    const SignalCacheKey canonicalLhs = canonicalizeKey(lhs);
    const SignalCacheKey canonicalRhs = canonicalizeKey(rhs);

    return static_cast<uint8_t>(canonicalLhs.mode) == static_cast<uint8_t>(canonicalRhs.mode)
        && equalsDateRange(canonicalLhs.dateRange, canonicalRhs.dateRange)
        && equalsPostProcessingConfig(canonicalLhs.postProcessingConfig, canonicalRhs.postProcessingConfig)
        && equalsVectorValues(canonicalLhs.instruments, canonicalRhs.instruments, [](const InstrumentId id) { return id.value; })
        && equalsVectorValues(canonicalLhs.factors, canonicalRhs.factors, [](const FactorId id) { return id.value; });
}

SignalCache::SignalCache(uint64_t maxMemoryBytes) noexcept
    : maxMemoryBytes_(maxMemoryBytes)
{
}

uint64_t SignalCache::estimateSignalSetMemory(const SignalSet& signalSet) noexcept
{
    // 估算公式（设计文档 Section 6.2）：
    // base_bytes = T * N * F * sizeof(double)
    // mask_bytes = T * N * F * sizeof(uint8_t)
    // + dates 数组 + instruments 数组 + factors 数组
    uint64_t bytes = 0U;
    bytes += signalSet.dates.size() * sizeof(DateKey);
    bytes += signalSet.instruments.size() * sizeof(InstrumentId);
    bytes += signalSet.factors.size() * sizeof(FactorId);
    bytes += signalSet.values.size() * sizeof(double);
    bytes += signalSet.mask.size() * sizeof(uint8_t);
    bytes += sizeof(SignalSet);
    return bytes;
}

SignalCacheKey SignalCache::canonicalizeKey(const SignalCacheKey& key)
{
    SignalCacheKey canonical;
    canonical.mode = key.mode;
    canonical.dateRange = key.dateRange;
    canonical.postProcessingConfig = key.postProcessingConfig;
    canonical.instruments = canonicalizeVector(key.instruments, [](const InstrumentId id) { return id.value; });
    canonical.factors = canonicalizeVector(key.factors, [](const FactorId id) { return id.value; });
    return canonical;
}

void SignalCache::evictIfNeeded(std::lock_guard<std::mutex>& /*lock*/) noexcept
{
    if (maxMemoryBytes_ == 0U) {
        return;
    }

    // LRU 淘汰：从链表尾端（最久未访问）开始逐出
    while (currentMemoryBytes_ > maxMemoryBytes_ && !lruList_.empty()) {
        const SignalCacheKey& evictKey = lruList_.back();
        const auto iter = storage_.find(evictKey);
        if (iter != storage_.end()) {
            currentMemoryBytes_ -= iter->second.memoryBytes;
            storage_.erase(iter);
        }
        lruList_.pop_back();
    }
}

std::optional<SignalSet> SignalCache::load(const SignalCacheKey& key) const
{
    const SignalCacheKey canonical = canonicalizeKey(key);

    std::lock_guard<std::mutex> lock(mutex_);
    const auto iter = storage_.find(canonical);
    if (iter == storage_.end()) {
        return std::nullopt;
    }

    // LRU 更新：将访问条目移到链表头部（最新）
    // 注意：需要在 non-const 的 lruList_ 上操作，
    // 但 lruList_ 标记为 mutable 的关键字段只有迭代器位置
    // 当前简化实现：load 时更新 LRU 位置
    lruList_.splice(lruList_.begin(), lruList_, iter->second.lruPosition);

    return iter->second.signalSet;
}

void SignalCache::store(const SignalCacheKey& key, const SignalSet& signalSet)
{
    const SignalCacheKey canonical = canonicalizeKey(key);
    const uint64_t entryMemory = estimateSignalSetMemory(signalSet);

    std::lock_guard<std::mutex> lock(mutex_);

    // 如果键已存在，先扣减旧条目的内存
    const auto existingIter = storage_.find(canonical);
    if (existingIter != storage_.end()) {
        currentMemoryBytes_ -= existingIter->second.memoryBytes;
        lruList_.erase(existingIter->second.lruPosition);
        storage_.erase(existingIter);
    }

    // 存入新条目
    lruList_.push_front(canonical);
    CacheEntry entry;
    entry.signalSet = signalSet;
    entry.memoryBytes = entryMemory;
    entry.lruPosition = lruList_.begin();
    storage_[canonical] = std::move(entry);
    currentMemoryBytes_ += entryMemory;

    // LRU 淘汰
    evictIfNeeded(lock);
}

uint64_t SignalCache::currentMemoryBytes() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return currentMemoryBytes_;
}

size_t SignalCache::entryCount() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return storage_.size();
}

} // namespace factor::compute