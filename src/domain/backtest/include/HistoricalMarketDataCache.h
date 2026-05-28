#pragma once

#include "StrategyBacktestEngineInterfaces.h"
#include "market/core/IDataProvider.h"

#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace domain::backtest {

struct TradingDayTimeRange final {
    std::uint64_t startTimestamp{0};
    std::uint64_t endTimestamp{0};

    [[nodiscard]] bool isValid() const
    {
        return startTimestamp > 0U && endTimestamp >= startTimestamp;
    }
};

class ITradingDayRangeResolver {
public:
    virtual ~ITradingDayRangeResolver() = default;

    [[nodiscard]] virtual std::optional<TradingDayTimeRange> resolve(
        strategy_engine::TradingDayIndex tradingDay) const = 0;
};

    class IFactorSnapshotProvider {
    public:
        virtual ~IFactorSnapshotProvider() = default;

        [[nodiscard]] virtual strategy_engine::FactorSnapshotList loadSnapshots(
            strategy_engine::OverlayBindingScopeId overlayBindingScopeId,
            strategy_engine::TradingDayIndex tradingDay,
            const strategy_engine::SymbolIdList& symbols,
            const strategy_engine::FactorIdList& factorIds) const = 0;
    };

class IndexedTradingDayRangeResolver final : public ITradingDayRangeResolver {
public:
    IndexedTradingDayRangeResolver(strategy_engine::TradingDayIndex firstTradingDay,
                                   std::vector<TradingDayTimeRange> timeRanges);

    [[nodiscard]] std::optional<TradingDayTimeRange> resolve(
        strategy_engine::TradingDayIndex tradingDay) const override;

private:
    strategy_engine::TradingDayIndex firstTradingDay_;
    std::vector<TradingDayTimeRange> timeRanges_;
};

class HistoricalMarketDataCache final : public strategy_engine::IMarketDataCache {
public:
    HistoricalMarketDataCache(astock::market::IDataProvider& dataProvider,
                              const ITradingDayRangeResolver& tradingDayRangeResolver,
                              std::uint32_t period,
                              strategy_engine::CandidateCount warmupDayCount,
                              const IFactorSnapshotProvider* factorSnapshotProvider = nullptr);

    [[nodiscard]] strategy_engine::MarketDataSlice sliceForDay(
        strategy_engine::OverlayBindingScopeId overlayBindingScopeId,
        strategy_engine::TradingDayIndex tradingDay,
        const strategy_engine::SymbolIdList& symbols,
        const strategy_engine::FactorIdList& factorIds) const override;

    [[nodiscard]] strategy_engine::CandidateCount warmupDayCount() const override;

private:
    using TradingDayIndex = strategy_engine::TradingDayIndex;
    using MarketBar = strategy_engine::MarketBar;
    using MarketDataSlice = strategy_engine::MarketDataSlice;
    using SymbolId = strategy_engine::SymbolId;
    using SymbolIdList = strategy_engine::SymbolIdList;
    using UniverseId = strategy_engine::UniverseId;

    [[nodiscard]] bool primeBarsForDay(TradingDayIndex tradingDay,
                                       const SymbolIdList& symbols) const;
    [[nodiscard]] std::optional<MarketBar> cachedBarForDay(TradingDayIndex tradingDay,
                                                           SymbolId symbolId) const;
    [[nodiscard]] static UniverseId buildSliceUniverseId(const SymbolIdList& symbols);
    [[nodiscard]] static std::uint64_t buildCacheKey(TradingDayIndex tradingDay, SymbolId symbolId);

    astock::market::IDataProvider& dataProvider_;
    const ITradingDayRangeResolver& tradingDayRangeResolver_;
    const IFactorSnapshotProvider* factorSnapshotProvider_{nullptr};
    std::uint32_t period_{0};
    strategy_engine::CandidateCount warmupDayCount_;

    mutable std::shared_mutex cacheMutex_;
    mutable std::unordered_map<std::uint64_t, std::optional<MarketBar>> barCache_;
};

} // namespace domain::backtest