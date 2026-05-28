#include "HistoricalMarketDataCache.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace domain::backtest {

namespace {

using CandidateCount = strategy_engine::CandidateCount;
using MarketBar = strategy_engine::MarketBar;
using PriceValue = strategy_engine::PriceValue;
using ShareQuantity = strategy_engine::ShareQuantity;
using SymbolId = strategy_engine::SymbolId;
using TradingDayIndex = strategy_engine::TradingDayIndex;

[[nodiscard]] ShareQuantity toShareQuantity(const float volume)
{
    if (!std::isfinite(volume) || volume <= 0.0f) {
        return ShareQuantity{};
    }

    const auto roundedVolume = static_cast<std::uint64_t>(std::llround(volume));
    return ShareQuantity(roundedVolume);
}

[[nodiscard]] bool isFinitePositive(const double value)
{
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool isValidMarketKLine(const astock::market::KLine& kline,
                                      const SymbolId symbolId)
{
    return kline.is_valid()
        && kline.symbol_id == symbolId.value()
        && isFinitePositive(kline.open)
        && isFinitePositive(kline.high)
        && isFinitePositive(kline.low)
        && isFinitePositive(kline.close);
}

void validateIndexedTimeRanges(const std::vector<TradingDayTimeRange>& timeRanges)
{
    if (timeRanges.empty()) {
        throw std::invalid_argument("indexed trading day range resolver requires time ranges");
    }

    for (std::size_t index = 0; index < timeRanges.size(); ++index) {
        const TradingDayTimeRange& timeRange = timeRanges[index];
        if (!timeRange.isValid()) {
            throw std::invalid_argument("indexed trading day range resolver received invalid time range");
        }

        if (index > 0U && timeRanges[index - 1U].endTimestamp >= timeRange.startTimestamp) {
            throw std::invalid_argument("indexed trading day range resolver requires strictly increasing time ranges");
        }
    }
}

[[nodiscard]] MarketBar toMarketBar(const astock::market::KLine& kline,
                                    const TradingDayIndex tradingDay,
                                    const SymbolId symbolId)
{
    return MarketBar{symbolId,
                     PriceValue(kline.open),
                     PriceValue(kline.high),
                     PriceValue(kline.low),
                     PriceValue(kline.close),
                     toShareQuantity(kline.volume),
                     tradingDay};
}

} // namespace

IndexedTradingDayRangeResolver::IndexedTradingDayRangeResolver(
    const TradingDayIndex firstTradingDay,
    std::vector<TradingDayTimeRange> timeRanges)
    : firstTradingDay_(firstTradingDay)
    , timeRanges_(std::move(timeRanges))
{
    if (!firstTradingDay_.isValid()) {
        throw std::invalid_argument("indexed trading day range resolver requires a valid first trading day");
    }

    validateIndexedTimeRanges(timeRanges_);
}

std::optional<TradingDayTimeRange> IndexedTradingDayRangeResolver::resolve(
    const TradingDayIndex tradingDay) const
{
    if (!tradingDay.isValid() || tradingDay.value() < firstTradingDay_.value()) {
        return std::nullopt;
    }

    const auto offset = static_cast<std::size_t>(tradingDay.value() - firstTradingDay_.value());
    if (offset >= timeRanges_.size()) {
        return std::nullopt;
    }

    return timeRanges_[offset];
}

HistoricalMarketDataCache::HistoricalMarketDataCache(
    astock::market::IDataProvider& dataProvider,
    const ITradingDayRangeResolver& tradingDayRangeResolver,
    const std::uint32_t period,
    const CandidateCount warmupDayCount,
    const IFactorSnapshotProvider* factorSnapshotProvider)
    : dataProvider_(dataProvider)
    , tradingDayRangeResolver_(tradingDayRangeResolver)
    , factorSnapshotProvider_(factorSnapshotProvider)
    , period_(period)
    , warmupDayCount_(warmupDayCount)
{
    if (period_ == 0U || !warmupDayCount_.isPositive()) {
        throw std::invalid_argument("invalid historical market data cache configuration");
    }
}

strategy_engine::MarketDataSlice HistoricalMarketDataCache::sliceForDay(
    const strategy_engine::OverlayBindingScopeId overlayBindingScopeId,
    const TradingDayIndex tradingDay,
    const SymbolIdList& symbols,
    const strategy_engine::FactorIdList& factorIds) const
{
    if (!tradingDay.isValid() || symbols.empty()) {
        return MarketDataSlice{};
    }

    if (!primeBarsForDay(tradingDay, symbols)) {
        return MarketDataSlice{};
    }

    MarketDataSlice slice;
    slice.tradingDay = tradingDay;
    slice.universeId = buildSliceUniverseId(symbols);

    for (const SymbolId symbolId : symbols) {
        const std::optional<MarketBar> marketBar = cachedBarForDay(tradingDay, symbolId);
        if (!marketBar.has_value()) {
            return MarketDataSlice{};
        }

        slice.bars.add(*marketBar);
    }

    if (!factorIds.empty()) {
        if (!factorSnapshotProvider_) {
            return MarketDataSlice{};
        }

        slice.factorSnapshots = factorSnapshotProvider_->loadSnapshots(overlayBindingScopeId,
                                                                       tradingDay,
                                                                       symbols,
                                                                       factorIds);
    }

    return slice;
}

strategy_engine::CandidateCount HistoricalMarketDataCache::warmupDayCount() const
{
    return warmupDayCount_;
}

bool HistoricalMarketDataCache::primeBarsForDay(const TradingDayIndex tradingDay,
                                                const SymbolIdList& symbols) const
{
    const std::optional<TradingDayTimeRange> timeRange = tradingDayRangeResolver_.resolve(tradingDay);
    if (!timeRange.has_value() || !timeRange->isValid()) {
        return false;
    }

    std::vector<SymbolId> missingSymbols;
    missingSymbols.reserve(symbols.size());
    {
        std::shared_lock<std::shared_mutex> lock(cacheMutex_);
        for (const SymbolId symbolId : symbols) {
            const std::uint64_t cacheKey = buildCacheKey(tradingDay, symbolId);
            if (barCache_.find(cacheKey) == barCache_.end()) {
                missingSymbols.push_back(symbolId);
            }
        }
    }

    if (missingSymbols.empty()) {
        return true;
    }

    std::vector<std::uint32_t> symbolIds;
    symbolIds.reserve(missingSymbols.size());
    for (const SymbolId symbolId : missingSymbols) {
        symbolIds.push_back(symbolId.value());
    }

    const astock::market::KLineBatch batch = dataProvider_.get_history_klines_batch(symbolIds,
                                                                                     period_,
                                                                                     timeRange->startTimestamp,
                                                                                     timeRange->endTimestamp,
                                                                                     1U);

    std::unordered_map<std::uint32_t, std::optional<MarketBar>> loadedBars;
    loadedBars.reserve(missingSymbols.size());
    for (const astock::market::KLine& kline : batch) {
        const SymbolId symbolId(kline.symbol_id);
        if (!isValidMarketKLine(kline, symbolId)) {
            continue;
        }

        MarketBar marketBar = toMarketBar(kline, tradingDay, symbolId);
        if (!marketBar.isValid()) {
            continue;
        }

        loadedBars.insert_or_assign(kline.symbol_id, marketBar);
    }

    std::unique_lock<std::shared_mutex> lock(cacheMutex_);
    for (const SymbolId symbolId : missingSymbols) {
        const std::uint64_t cacheKey = buildCacheKey(tradingDay, symbolId);
        if (barCache_.find(cacheKey) != barCache_.end()) {
            continue;
        }

        const auto iterator = loadedBars.find(symbolId.value());
        if (iterator != loadedBars.end()) {
            barCache_.emplace(cacheKey, iterator->second);
            continue;
        }

        barCache_.emplace(cacheKey, std::nullopt);
    }

    return true;
}

std::optional<HistoricalMarketDataCache::MarketBar> HistoricalMarketDataCache::cachedBarForDay(
    const TradingDayIndex tradingDay,
    const SymbolId symbolId) const
{
    const std::uint64_t cacheKey = buildCacheKey(tradingDay, symbolId);
    std::shared_lock<std::shared_mutex> lock(cacheMutex_);
    const auto iterator = barCache_.find(cacheKey);
    if (iterator == barCache_.end()) {
        return std::nullopt;
    }

    return iterator->second;
}

strategy_engine::UniverseId HistoricalMarketDataCache::buildSliceUniverseId(const SymbolIdList& symbols)
{
    std::uint64_t hashValue = 1469598103934665603ULL;
    for (const SymbolId symbolId : symbols) {
        hashValue ^= static_cast<std::uint64_t>(symbolId.value());
        hashValue *= 1099511628211ULL;
    }

    if (hashValue == 0ULL) {
        hashValue = 1ULL;
    }

    return UniverseId(hashValue);
}

std::uint64_t HistoricalMarketDataCache::buildCacheKey(const TradingDayIndex tradingDay,
                                                       const SymbolId symbolId)
{
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tradingDay.value())) << 32U)
        | static_cast<std::uint64_t>(symbolId.value());
}

} // namespace domain::backtest