#pragma once

#include "ISignalCache.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <unordered_map>

namespace factor::compute {

/// @brief 因子信号缓存（设计文档 Section 5.5）
///
/// 功能：
/// - 键：(DateRange, InstrumentSet, FactorSet) 的哈希。
/// - 值：shared_ptr<SignalSet>，带时间戳。
/// - 淘汰策略：LRU + 最大内存容量限制（默认可配置）。
/// - 键规范化：InstrumentSet 和 FactorSet 在构造键前按稳定顺序排序并去重。
class SignalCache final : public ISignalCache {
public:
    /// @brief 构造缓存
    ///
    /// @param maxMemoryBytes 最大内存限额（字节），默认 0 表示不设硬上限
    explicit SignalCache(uint64_t maxMemoryBytes = 0U);

    [[nodiscard]] std::optional<SignalSet>
    load(const SignalCacheKey& key) const override;

    void
    store(const SignalCacheKey& key, const SignalSet& signalSet) override;

    /// @brief 当前已用内存估算（字节）
    [[nodiscard]] uint64_t currentMemoryBytes() const noexcept;

    /// @brief 当前缓存条目数
    [[nodiscard]] size_t entryCount() const noexcept;

private:
    struct SignalCacheKeyHash final {
        [[nodiscard]] size_t operator()(const SignalCacheKey& key) const noexcept;
    };

    struct SignalCacheKeyEqual final {
        [[nodiscard]] bool operator()(const SignalCacheKey& lhs, const SignalCacheKey& rhs) const noexcept;
    };

    /// @brief 估算一个 SignalSet 的内存占用（字节）
    [[nodiscard]] static uint64_t estimateSignalSetMemory(const SignalSet& signalSet) noexcept;

    /// @brief 规范化缓存键：排序并去重 instruments 和 factors
    [[nodiscard]] static SignalCacheKey canonicalizeKey(const SignalCacheKey& key);

    /// @brief LRU 淘汰：持续逐出最久未访问条目直到内存低于上限
    void evictIfNeeded(std::lock_guard<std::mutex>& lock) noexcept;

    // LRU 链表节点
    using LruIterator = typename std::list<SignalCacheKey>::iterator;

    struct CacheEntry final {
        SignalSet signalSet;
        uint64_t memoryBytes{0U};
        LruIterator lruPosition;
    };

    uint64_t maxMemoryBytes_;
    uint64_t currentMemoryBytes_{0U};
    mutable std::mutex mutex_;
    std::unordered_map<SignalCacheKey, CacheEntry, SignalCacheKeyHash, SignalCacheKeyEqual> storage_;
    mutable std::list<SignalCacheKey> lruList_;
};

} // namespace factor::compute
