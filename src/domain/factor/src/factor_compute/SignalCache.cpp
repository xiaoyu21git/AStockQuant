#include "factor_compute/SignalCache.h"

#include <algorithm>
#include <cstring>

#include "xxhash.h"

namespace factor::compute {

namespace {

uint64_t bitwiseDouble(double value)
{
    uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(double));
    return bits;
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

    // 使用 xxHash64 替代 FNV-1a，预期 10× 加速
    // XXH64_state_t 是不透明类型，必须通过堆分配使用
    XXH64_state_t* state = XXH64_createState();
    if (!state) {
        // 退化到简单哈希
        size_t fallback = 0U;
        fallback ^= static_cast<size_t>(canonical.dateRange.from.value);
        fallback ^= static_cast<size_t>(canonical.dateRange.to.value);
        fallback ^= static_cast<size_t>(canonical.mode);
        for (const auto& inst : canonical.instruments) {
            fallback ^= static_cast<size_t>(inst.value);
        }
        for (const auto& fac : canonical.factors) {
            fallback ^= static_cast<size_t>(fac.value);
        }
        return fallback;
    }

    XXH64_reset(state, 0);

    // 哈希基础字段
    XXH64_update(state, &canonical.dateRange.from.value, sizeof(canonical.dateRange.from.value));
    XXH64_update(state, &canonical.dateRange.to.value, sizeof(canonical.dateRange.to.value));
    uint8_t mode = static_cast<uint8_t>(canonical.mode);
    XXH64_update(state, &mode, sizeof(mode));
    uint64_t winsorizeBits = bitwiseDouble(canonical.postProcessingConfig.winsorizeStdBand);
    uint64_t epsilonBits = bitwiseDouble(canonical.postProcessingConfig.stdEpsilon);
    XXH64_update(state, &winsorizeBits, sizeof(winsorizeBits));
    XXH64_update(state, &epsilonBits, sizeof(epsilonBits));
    XXH64_update(state, &canonical.postProcessingConfig.minimumValidSampleCount,
        sizeof(canonical.postProcessingConfig.minimumValidSampleCount));

    // 哈希仪器列表
    const size_t instrumentCount = canonical.instruments.size();
    XXH64_update(state, &instrumentCount, sizeof(instrumentCount));
    for (const auto& inst : canonical.instruments) {
        uint32_t val = inst.value;
        XXH64_update(state, &val, sizeof(val));
    }

    // 哈希因子列表
    const size_t factorCount = canonical.factors.size();
    XXH64_update(state, &factorCount, sizeof(factorCount));
    for (const auto& fac : canonical.factors) {
        uint32_t val = fac.value;
        XXH64_update(state, &val, sizeof(val));
    }

    const size_t result = static_cast<size_t>(XXH64_digest(state));
    XXH64_freeState(state);
    return result;
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
    bytes += signalSet.signalIds.size() * sizeof(SignalId);
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