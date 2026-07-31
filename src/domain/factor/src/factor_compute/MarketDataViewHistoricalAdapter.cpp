#include "factor_compute/MarketDataViewHistoricalAdapter.h"
#include "foundation/log/logging.hpp"

namespace factor::compute {

CachedMarketDataViewHistoricalAdapter::CachedMarketDataViewHistoricalAdapter(
    const factor::compute::IMarketDataView& marketDataView)
    : view_(marketDataView)
{
    const auto& viewDates = view_.dates();
    const auto& viewInstruments = view_.instruments();

    dates_.reserve(viewDates.size());
    for (size_t i = 0; i < viewDates.size(); ++i) {
        std::string dateStr = std::to_string(viewDates[i].value);
        dates_.push_back(dateStr);
        dateToIndex_[dateStr] = static_cast<int32_t>(i);
    }

    symbols_.reserve(viewInstruments.size());
    const auto& realSymbols = view_.symbolStrings();
    if (realSymbols.empty()) {
        INTERNAL_ERROR_STREAM << "[Adapter] symbolStrings() 为空，无法建立符号映射";
        return;
    }
    for (size_t i = 0; i < viewInstruments.size() && i < realSymbols.size(); ++i) {
        symbols_.push_back(realSymbols[i]);
        symbolToIndex_[realSymbols[i]] = static_cast<int32_t>(i);
    }
}

int32_t CachedMarketDataViewHistoricalAdapter::findDateIndex(const std::string& date) const
{
    auto it = dateToIndex_.find(date);
    if (it != dateToIndex_.end()) return it->second;
    // 支持 YYYY-MM-DD → YYYYMMDD 格式转换
    if (date.size() == 10 && date[4] == '-' && date[7] == '-') {
        std::string compact;
        compact.reserve(8);
        for (char c : date) { if (c >= '0' && c <= '9') compact += c; }
        auto it2 = dateToIndex_.find(compact);
        if (it2 != dateToIndex_.end()) return it2->second;
    }
    return -1;
}

int32_t CachedMarketDataViewHistoricalAdapter::findSymbolIndex(const std::string& symbol) const
{
    auto it = symbolToIndex_.find(symbol);
    return (it != symbolToIndex_.end()) ? it->second : -1;
}

bool CachedMarketDataViewHistoricalAdapter::hasField(const std::string& field) const
{
    return view_.getField(field).has_value();
}

std::optional<double> CachedMarketDataViewHistoricalAdapter::getValue(
    const std::string& symbol, const std::string& date, const std::string& field) const
{
    int32_t dateIdx = findDateIndex(date);
    int32_t symIdx = findSymbolIndex(symbol);
    if (dateIdx < 0 || symIdx < 0) return std::nullopt;

    auto fieldView = view_.getField(field);
    if (!fieldView.has_value()) return std::nullopt;
    const auto& fv = fieldView.value();
    if (!fv.isValid() || dateIdx >= fv.rowCount || symIdx >= fv.columnCount) return std::nullopt;
    const size_t stride = static_cast<size_t>(fv.rowStride >= fv.columnCount ? fv.rowStride : fv.columnCount);
    return fv.data[static_cast<size_t>(dateIdx) * stride + static_cast<size_t>(symIdx)];
}

std::vector<factor::HistoricalDataPoint> CachedMarketDataViewHistoricalAdapter::getSeries(
    const std::string& symbol, const std::string& startDate, const std::string& endDate,
    const std::string& field, bool includeNaN) const
{
    std::vector<factor::HistoricalDataPoint> result;
    int32_t symIdx = findSymbolIndex(symbol);
    if (symIdx < 0) return result;

    auto fieldView = view_.getField(field);
    if (!fieldView.has_value()) return result;
    const auto& fv = fieldView.value();
    if (!fv.isValid() || symIdx >= fv.columnCount) return result;

    int32_t startIdx = findDateIndex(startDate);
    int32_t endIdx   = findDateIndex(endDate);
    if (startIdx < 0) startIdx = 0;
    if (endIdx < 0 || endIdx >= fv.rowCount) endIdx = fv.rowCount - 1;
    if (startIdx > endIdx) return result;

    const int32_t stride = fv.rowStride >= fv.columnCount ? fv.rowStride : fv.columnCount;
    for (int32_t d = startIdx; d <= endIdx; ++d) {
        double val = fv.data[static_cast<size_t>(d) * static_cast<size_t>(stride) + static_cast<size_t>(symIdx)];
        if (includeNaN || std::isfinite(val))
            result.push_back({dates_[static_cast<size_t>(d)], val});
    }
    return result;
}

std::vector<factor::HistoricalDataPoint> CachedMarketDataViewHistoricalAdapter::getSeries(
    const std::string& symbol, const std::string& anchorDate, int window,
    const std::string& field, bool includeNaN) const
{
    int32_t anchorIdx = findDateIndex(anchorDate);
    if (anchorIdx < 0 || window <= 0) return {};
    int32_t startIdx = std::max(0, anchorIdx - window + 1);
    return getSeries(symbol, dates_[static_cast<size_t>(startIdx)], anchorDate, field, includeNaN);
}

std::vector<std::string> CachedMarketDataViewHistoricalAdapter::getAvailableSymbols(const std::string&) const
{
    return symbols_;
}

std::unordered_map<std::string, double> CachedMarketDataViewHistoricalAdapter::getCrossSection(
    const std::string& date, const std::string& field,
    const std::vector<std::string>& symbols) const
{
    std::unordered_map<std::string, double> result;
    int32_t dateIdx = findDateIndex(date);
    if (dateIdx >= 0) {
        auto fieldView = view_.getField(field);
        if (fieldView.has_value()) {
            const auto& fv = fieldView.value();
            if (fv.isValid() && dateIdx < fv.rowCount) {
                const int32_t rowStride = fv.rowStride >= fv.columnCount ? fv.rowStride : fv.columnCount;
                if (symbols.empty()) {
                    for (int32_t i = 0; i < fv.columnCount; ++i)
                        result[symbols_[static_cast<size_t>(i)]]
                            = fv.data[static_cast<size_t>(dateIdx) * static_cast<size_t>(rowStride) + static_cast<size_t>(i)];
                } else {
                    for (const auto& sym : symbols) {
                        int32_t si = findSymbolIndex(sym);
                        if (si < 0) continue;
                        result[sym] = fv.data[static_cast<size_t>(dateIdx) * static_cast<size_t>(rowStride) + static_cast<size_t>(si)];
                    }
                }
            }
        }
    }

    // Cache miss / 全 NaN → DB fallback
    if (!result.empty()) {
        bool hasFinite = false;
        for (const auto& [_, v] : result)
            if (std::isfinite(v)) { hasFinite = true; break; }
        if (hasFinite) return result;
    }
    if (dbFallback_)
        result = dbFallback_(date, field, symbols);
    return result;
}

std::unordered_map<std::string, std::unordered_map<std::string, double>>
CachedMarketDataViewHistoricalAdapter::getBatchCrossSections(
    const std::string& date, const std::vector<std::string>& symbols,
    const std::vector<std::string>& fields) const
{
    std::unordered_map<std::string, std::unordered_map<std::string, double>> result;
    for (const auto& field : fields)
        result[field] = getCrossSection(date, field, symbols);
    return result;
}

} // namespace factor::compute
