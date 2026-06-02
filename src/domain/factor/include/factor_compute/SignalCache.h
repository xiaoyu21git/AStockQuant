#pragma once

#include "ISignalCache.h"

#include <cstddef>
#include <mutex>
#include <unordered_map>

namespace factor::compute {

class SignalCache final : public ISignalCache {
public:
    [[nodiscard]] std::optional<SignalSet>
    load(const SignalCacheKey& key) const override;

    void
    store(const SignalCacheKey& key, const SignalSet& signalSet) override;

private:
    struct SignalCacheKeyHash final {
        [[nodiscard]] size_t operator()(const SignalCacheKey& key) const noexcept;
    };

    struct SignalCacheKeyEqual final {
        [[nodiscard]] bool operator()(const SignalCacheKey& lhs, const SignalCacheKey& rhs) const noexcept;
    };

    mutable std::mutex mutex_;
    std::unordered_map<SignalCacheKey, SignalSet, SignalCacheKeyHash, SignalCacheKeyEqual> storage_;
};

} // namespace factor::compute
