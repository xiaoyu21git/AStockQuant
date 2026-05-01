#include "domain/factor/include/ArrowMarketData.h"
#include "domain/factor/include/FactorBacktestCachedBarUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace factor {

namespace {

void throwArrowStatus(const arrow::Status& status, const char* action)
{
    if (!status.ok()) {
        throw std::runtime_error(std::string("ArrowMarketData: ") + action + ": " + status.ToString());
    }
}

bool isValidNumericValue(double value)
{
    return std::isfinite(value);
}

std::vector<std::string> deduplicateSymbolsPreservingOrder(const std::vector<std::string>& symbols)
{
    std::vector<std::string> uniqueSymbols;
    uniqueSymbols.reserve(symbols.size());

    std::unordered_set<std::string> seen;
    seen.reserve(symbols.size());
    for (const auto& symbol : symbols) {
        if (seen.insert(symbol).second) {
            uniqueSymbols.push_back(symbol);
        }
    }

    return uniqueSymbols;
}

std::vector<int64_t> selectRowsByDates(const std::vector<std::string>& sortedDates,
                                       const std::vector<int64_t>& rows,
                                       const std::string& startDate,
                                       const std::string& endDate)
{
    std::vector<int64_t> selectedRows;
    if (sortedDates.empty() || rows.empty()) {
        return selectedRows;
    }

    // dates 与 rows 保持相同顺序，因此日期范围可以直接映射成连续行段。
    const auto startIt = std::lower_bound(sortedDates.begin(), sortedDates.end(), startDate);
    const auto endIt = std::upper_bound(sortedDates.begin(), sortedDates.end(), endDate);
    const size_t beginIndex = static_cast<size_t>(std::distance(sortedDates.begin(), startIt));
    const size_t endIndex = static_cast<size_t>(std::distance(sortedDates.begin(), endIt));
    if (beginIndex >= endIndex || beginIndex >= rows.size()) {
        return selectedRows;
    }

    const size_t boundedEnd = std::min(endIndex, rows.size());
    selectedRows.reserve(boundedEnd - beginIndex);
    for (size_t index = beginIndex; index < boundedEnd; ++index) {
        selectedRows.push_back(rows[index]);
    }
    return selectedRows;
}

} // namespace

std::shared_ptr<ArrowMarketData> ArrowMarketData::fromCachedBars(const std::vector<factor::CachedMarketBar>& bars)
{
    auto data = std::shared_ptr<ArrowMarketData>(new ArrowMarketData());

    // 先把原始缓存行归一化并排序，保证后续 Arrow 列和索引使用同一行顺序。
    std::vector<NormalizedBar> normalizedBars;
    normalizedBars.reserve(bars.size());

    std::set<std::string> fieldNames;
    fieldNames.insert("close");

    for (const auto& bar : bars) {
        const std::string normalizedDate = factor::cached_bars::normalizeTradeDate(bar.tradeDate);
        if (normalizedDate.empty() || bar.symbol.empty()) {
            continue;
        }

        NormalizedBar normalizedBar;
        normalizedBar.symbol = bar.symbol;
        normalizedBar.tradeDate = normalizedDate;
        normalizedBar.close = bar.close;
        normalizedBar.numericFields = bar.numericFields;
        normalizedBars.push_back(std::move(normalizedBar));

        for (const auto& [field, value] : bar.numericFields) {
            (void)value;
            fieldNames.insert(field);
        }
    }

    std::sort(normalizedBars.begin(), normalizedBars.end(), [](const NormalizedBar& lhs, const NormalizedBar& rhs) {
        if (lhs.tradeDate != rhs.tradeDate) {
            return lhs.tradeDate < rhs.tradeDate;
        }
        return lhs.symbol < rhs.symbol;
    });

    data->fieldNames_.assign(fieldNames.begin(), fieldNames.end());

    std::set<std::string> uniqueSymbols;
    std::set<std::string> uniqueDates;

    std::unordered_map<std::string, std::shared_ptr<arrow::DoubleBuilder>> numericBuilders;
    numericBuilders.reserve(data->fieldNames_.size());
    for (const auto& fieldName : data->fieldNames_) {
        if (fieldName == "close") {
            continue;
        }
        numericBuilders.emplace(fieldName, std::make_shared<arrow::DoubleBuilder>());
    }

    arrow::StringBuilder symbolBuilder;
    arrow::StringBuilder dateBuilder;
    arrow::DoubleBuilder closeBuilder;

    for (size_t rowIndex = 0; rowIndex < normalizedBars.size(); ++rowIndex) {
        const auto& bar = normalizedBars[rowIndex];

        throwArrowStatus(symbolBuilder.Append(bar.symbol), "append symbol");
        throwArrowStatus(dateBuilder.Append(bar.tradeDate), "append trade date");
        if (isValidNumericValue(bar.close) && bar.close > 0.0) {
            throwArrowStatus(closeBuilder.Append(bar.close), "append close");
        } else {
            throwArrowStatus(closeBuilder.AppendNull(), "append close null");
        }

        uniqueSymbols.insert(bar.symbol);
        uniqueDates.insert(bar.tradeDate);

        data->rowIndexByDateSymbol_[bar.tradeDate][bar.symbol] = static_cast<int64_t>(rowIndex);
        data->symbolToRowIndices_[bar.symbol].push_back(static_cast<int64_t>(rowIndex));
        data->symbolToDates_[bar.symbol].push_back(bar.tradeDate);
        data->dateToSymbols_[bar.tradeDate].push_back(bar.symbol);

        for (const auto& fieldName : data->fieldNames_) {
            if (fieldName == "close") {
                continue;
            }

            const auto fieldIt = bar.numericFields.find(fieldName);
            auto builderIt = numericBuilders.find(fieldName);
            if (builderIt == numericBuilders.end()) {
                continue;
            }

            if (fieldIt != bar.numericFields.end() && isValidNumericValue(fieldIt->second)) {
                throwArrowStatus(builderIt->second->Append(fieldIt->second), "append numeric field");
            } else {
                throwArrowStatus(builderIt->second->AppendNull(), "append numeric null");
            }
        }
    }

    std::shared_ptr<arrow::Array> symbolArray;
    std::shared_ptr<arrow::Array> dateArray;
    std::shared_ptr<arrow::Array> closeArray;
    throwArrowStatus(symbolBuilder.Finish(&symbolArray), "finish symbol array");
    throwArrowStatus(dateBuilder.Finish(&dateArray), "finish date array");
    throwArrowStatus(closeBuilder.Finish(&closeArray), "finish close array");

    std::vector<std::shared_ptr<arrow::Array>> arrays;
    arrays.reserve(data->fieldNames_.size() + 2);
    arrays.push_back(symbolArray);
    arrays.push_back(dateArray);
    arrays.push_back(closeArray);

    std::vector<std::shared_ptr<arrow::Field>> schemaFields;
    schemaFields.reserve(data->fieldNames_.size() + 2);
    schemaFields.push_back(arrow::field("symbol", arrow::utf8()));
    schemaFields.push_back(arrow::field("date", arrow::utf8()));
    schemaFields.push_back(arrow::field("close", arrow::float64()));

    for (const auto& fieldName : data->fieldNames_) {
        if (fieldName == "close") {
            continue;
        }

        auto builderIt = numericBuilders.find(fieldName);
        if (builderIt == numericBuilders.end()) {
            continue;
        }

        std::shared_ptr<arrow::Array> fieldArray;
        throwArrowStatus(builderIt->second->Finish(&fieldArray), "finish numeric array");
        arrays.push_back(fieldArray);
        schemaFields.push_back(arrow::field(fieldName, arrow::float64()));
    }

    auto schema = arrow::schema(schemaFields);
    data->table_ = arrow::Table::Make(schema, arrays, static_cast<int64_t>(normalizedBars.size()));
    if (!data->table_) {
        throw std::runtime_error("ArrowMarketData: failed to construct Arrow table");
    }

    data->symbols_.assign(uniqueSymbols.begin(), uniqueSymbols.end());
    data->dates_.assign(uniqueDates.begin(), uniqueDates.end());

    for (size_t index = 0; index < data->symbols_.size(); ++index) {
        data->symbolToIndex_[data->symbols_[index]] = static_cast<int>(index);
    }
    for (size_t index = 0; index < data->dates_.size(); ++index) {
        data->dateToIndex_[data->dates_[index]] = static_cast<int>(index);
    }
    return data;
}

std::shared_ptr<arrow::ChunkedArray> ArrowMarketData::getColumn(const std::string& field) const
{
    if (!table_) {
        return nullptr;
    }
    return table_->GetColumnByName(field);
}

double ArrowMarketData::getValue(const std::string& symbol,
                                 const std::string& date,
                                 const std::string& field) const
{
    static constexpr double kMissingValue = std::numeric_limits<double>::quiet_NaN();

    const auto dateIt = rowIndexByDateSymbol_.find(date);
    if (dateIt == rowIndexByDateSymbol_.end()) {
        return kMissingValue;
    }

    const auto symbolIt = dateIt->second.find(symbol);
    if (symbolIt == dateIt->second.end()) {
        return kMissingValue;
    }

    const auto column = getColumn(field);
    if (!column || column->num_chunks() == 0) {
        return kMissingValue;
    }

    const auto chunk = std::dynamic_pointer_cast<arrow::DoubleArray>(column->chunk(0));
    if (!chunk) {
        return kMissingValue;
    }

    const int64_t rowIndex = symbolIt->second;
    if (rowIndex < 0 || rowIndex >= chunk->length() || chunk->IsNull(rowIndex)) {
        return kMissingValue;
    }

    return chunk->Value(rowIndex);
}

std::shared_ptr<arrow::Array> ArrowMarketData::takeArray(const std::shared_ptr<arrow::Array>& values,
                                                         const std::vector<int64_t>& indices)
{
    if (!values || indices.empty()) {
        return nullptr;
    }

    arrow::Int64Builder indexBuilder;
    for (int64_t index : indices) {
        throwArrowStatus(indexBuilder.Append(index), "append take index");
    }

    std::shared_ptr<arrow::Array> indexArray;
    throwArrowStatus(indexBuilder.Finish(&indexArray), "finish take index array");

    auto taken = arrow::compute::Take(*values, *indexArray);
    if (!taken.ok()) {
        throw std::runtime_error(std::string("ArrowMarketData: take failed: ") + taken.status().ToString());
    }

    return std::move(taken).ValueOrDie();
}

std::vector<int64_t> ArrowMarketData::selectRowsForSymbol(const std::string& symbol,
                                                          const std::string& startDate,
                                                          const std::string& endDate) const
{
    const auto datesIt = symbolToDates_.find(symbol);
    const auto rowsIt = symbolToRowIndices_.find(symbol);
    if (datesIt == symbolToDates_.end() || rowsIt == symbolToRowIndices_.end()) {
        return {};
    }

    return selectRowsByDates(datesIt->second, rowsIt->second, startDate, endDate);
}

std::vector<int64_t> ArrowMarketData::selectRowsForSymbolWindow(const std::string& symbol,
                                                               const std::string& anchorDate,
                                                               int window) const
{
    if (window <= 0) {
        return {};
    }

    const auto datesIt = symbolToDates_.find(symbol);
    const auto rowsIt = symbolToRowIndices_.find(symbol);
    if (datesIt == symbolToDates_.end() || rowsIt == symbolToRowIndices_.end()) {
        return {};
    }

    const auto& dates = datesIt->second;
    const auto& rows = rowsIt->second;
    const auto anchorIt = std::upper_bound(dates.begin(), dates.end(), anchorDate);
    const size_t endIndex = static_cast<size_t>(std::distance(dates.begin(), anchorIt));
    if (endIndex == 0) {
        return {};
    }

    const size_t startIndex = endIndex > static_cast<size_t>(window)
        ? endIndex - static_cast<size_t>(window)
        : size_t{0};

    std::vector<int64_t> selectedRows;
    selectedRows.reserve(endIndex - startIndex);
    for (size_t index = startIndex; index < endIndex && index < rows.size(); ++index) {
        selectedRows.push_back(rows[index]);
    }
    return selectedRows;
}

std::shared_ptr<arrow::DoubleArray> ArrowMarketData::getTimeSeries(const std::string& symbol,
                                                                   const std::string& field,
                                                                   int lookbackWindow) const
{
    if (!table_ || lookbackWindow <= 0 || dates_.empty()) {
        return nullptr;
    }

    const auto rows = selectRowsForSymbolWindow(symbol, dates_.back(), lookbackWindow);
    if (rows.empty()) {
        return nullptr;
    }

    const auto column = getColumn(field);
    if (!column || column->num_chunks() == 0) {
        return nullptr;
    }

    const auto taken = takeArray(column->chunk(0), rows);
    return std::dynamic_pointer_cast<arrow::DoubleArray>(taken);
}

std::vector<std::vector<double>> ArrowMarketData::getBatchTimeSeries(const std::vector<std::string>& symbols,
                                                                     const std::string& field,
                                                                     int window,
                                                                     const std::string& anchorDate) const
{
    std::vector<std::vector<double>> matrix;
    if (!table_ || window <= 0) {
        return matrix;
    }

    const auto column = getColumn(field);
    if (!column || column->num_chunks() == 0) {
        return matrix;
    }

    const auto uniqueSymbols = deduplicateSymbolsPreservingOrder(symbols);

    // 批量接口按 symbol 作为结果 key，重复 symbol 没有额外信息，只会重复拼接同一条序列。
    std::vector<std::vector<int64_t>> rowsBySymbol;
    rowsBySymbol.reserve(uniqueSymbols.size());
    std::vector<int64_t> combinedRows;
    combinedRows.reserve(uniqueSymbols.size() * static_cast<size_t>(window));

    // 先批量收集所有 symbol 的行号，再一次性做 Take，避免逐 symbol 重复构建 Arrow 索引。
    for (const auto& symbol : uniqueSymbols) {
        auto rows = selectRowsForSymbolWindow(symbol, anchorDate, window);
        combinedRows.insert(combinedRows.end(), rows.begin(), rows.end());
        rowsBySymbol.push_back(std::move(rows));
    }

    matrix.resize(uniqueSymbols.size());
    if (combinedRows.empty()) {
        return matrix;
    }

    const auto taken = takeArray(column->chunk(0), combinedRows);
    const auto values = std::dynamic_pointer_cast<arrow::DoubleArray>(taken);
    if (!values) {
        return matrix;
    }

    size_t offset = 0;
    for (size_t symbolIndex = 0; symbolIndex < uniqueSymbols.size(); ++symbolIndex) {
        const auto& rows = rowsBySymbol[symbolIndex];
        auto& series = matrix[symbolIndex];
        series.reserve(rows.size());
        for (size_t rowOffset = 0; rowOffset < rows.size(); ++rowOffset) {
            const int64_t valueIndex = static_cast<int64_t>(offset + rowOffset);
            if (valueIndex >= values->length() || values->IsNull(valueIndex)) {
                continue;
            }
            series.push_back(values->Value(valueIndex));
        }
        offset += rows.size();
    }

    return matrix;
}

std::vector<FactorDataPoint> ArrowMarketData::getSeries(const std::string& symbol,
                                                        const std::string& startDate,
                                                        const std::string& endDate,
                                                        const std::string& field) const
{
    std::vector<FactorDataPoint> series;
    const auto rows = selectRowsForSymbol(symbol, startDate, endDate);
    if (rows.empty()) {
        return series;
    }

    const auto datesIt = symbolToDates_.find(symbol);
    if (datesIt == symbolToDates_.end()) {
        return series;
    }

    const auto column = getColumn(field);
    if (!column || column->num_chunks() == 0) {
        return series;
    }

    const auto taken = takeArray(column->chunk(0), rows);
    const auto values = std::dynamic_pointer_cast<arrow::DoubleArray>(taken);
    if (!values) {
        return series;
    }

    series.reserve(rows.size());
    for (size_t index = 0; index < rows.size(); ++index) {
        const int64_t valueIndex = static_cast<int64_t>(index);
        if (values->IsNull(valueIndex)) {
            continue;
        }
        series.push_back(FactorDataPoint{datesIt->second[index], values->Value(valueIndex)});
    }
    return series;
}

std::vector<std::string> ArrowMarketData::getAvailableSymbols(const std::string& date) const
{
    const auto it = dateToSymbols_.find(date);
    if (it == dateToSymbols_.end()) {
        return {};
    }

    std::vector<std::string> symbols = it->second;
    std::sort(symbols.begin(), symbols.end());
    symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());
    return symbols;
}

std::unordered_map<std::string, double> ArrowMarketData::getCrossSection(const std::string& date,
                                                                        const std::string& field,
                                                                        const std::vector<std::string>& symbols) const
{
    std::unordered_map<std::string, double> values;
    const auto dateIt = rowIndexByDateSymbol_.find(date);
    if (dateIt == rowIndexByDateSymbol_.end()) {
        return values;
    }

    const auto column = getColumn(field);
    if (!column || column->num_chunks() == 0) {
        return values;
    }

    const auto typed = std::dynamic_pointer_cast<arrow::DoubleArray>(column->chunk(0));
    if (!typed) {
        return values;
    }

    if (symbols.empty()) {
        values.reserve(dateIt->second.size());
        for (const auto& [symbol, rowIndex] : dateIt->second) {
            if (!typed->IsNull(rowIndex)) {
                values.emplace(symbol, typed->Value(rowIndex));
            }
        }
        return values;
    }

    values.reserve(symbols.size());
    for (const auto& symbol : symbols) {
        const auto symbolIt = dateIt->second.find(symbol);
        if (symbolIt == dateIt->second.end()) {
            continue;
        }

        const int64_t rowIndex = symbolIt->second;
        if (!typed->IsNull(rowIndex)) {
            values.emplace(symbol, typed->Value(rowIndex));
        }
    }

    return values;
}

std::unordered_map<std::string, std::unordered_map<std::string, double>> ArrowMarketData::getBatchCrossSections(
    const std::string& date,
    const std::vector<std::string>& symbols,
    const std::vector<std::string>& fields) const
{
    std::unordered_map<std::string, std::unordered_map<std::string, double>> batchValues;
    const auto dateIt = rowIndexByDateSymbol_.find(date);
    if (dateIt == rowIndexByDateSymbol_.end()) {
        return batchValues;
    }

    const bool useAllSymbols = symbols.empty();
    std::unordered_set<std::string> requestedSymbols;
    if (!useAllSymbols) {
        requestedSymbols.insert(symbols.begin(), symbols.end());
    }

    for (const auto& field : fields) {
        auto& fieldValues = batchValues[field];
        const auto column = getColumn(field);
        if (!column || column->num_chunks() == 0) {
            continue;
        }

        const auto typed = std::dynamic_pointer_cast<arrow::DoubleArray>(column->chunk(0));
        if (!typed) {
            continue;
        }

        if (useAllSymbols) {
            fieldValues.reserve(dateIt->second.size());
            for (const auto& [symbol, rowIndex] : dateIt->second) {
                if (!typed->IsNull(rowIndex)) {
                    fieldValues.emplace(symbol, typed->Value(rowIndex));
                }
            }
            continue;
        }

        fieldValues.reserve(requestedSymbols.size());
        for (const auto& symbol : requestedSymbols) {
            const auto symbolIt = dateIt->second.find(symbol);
            if (symbolIt == dateIt->second.end()) {
                continue;
            }

            const int64_t rowIndex = symbolIt->second;
            if (!typed->IsNull(rowIndex)) {
                fieldValues.emplace(symbol, typed->Value(rowIndex));
            }
        }
    }

    return batchValues;
}

std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> ArrowMarketData::getBatchTimeSeries(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate,
    const std::vector<std::string>& fields) const
{
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> batchSeries;
    if (!table_) {
        return batchSeries;
    }

    std::vector<std::string> effectiveSymbols;
    if (symbols.empty()) {
        effectiveSymbols = this->symbols_;
    } else {
        effectiveSymbols = deduplicateSymbolsPreservingOrder(symbols);
    }

    std::unordered_map<std::string, std::vector<int64_t>> rowsBySymbol;
    std::vector<int64_t> combinedRows;
    rowsBySymbol.reserve(effectiveSymbols.size());

    // 同样先把所有 symbol 的匹配行合并，后面再按 symbol 切回去。
    for (const auto& symbol : effectiveSymbols) {
        const auto rows = selectRowsForSymbol(symbol, startDate, endDate);
        combinedRows.insert(combinedRows.end(), rows.begin(), rows.end());
        rowsBySymbol.emplace(symbol, rows);
    }

    if (combinedRows.empty()) {
        return batchSeries;
    }

    for (const auto& field : fields) {
        const auto column = getColumn(field);
        if (!column || column->num_chunks() == 0) {
            continue;
        }

        const auto taken = takeArray(column->chunk(0), combinedRows);
        const auto values = std::dynamic_pointer_cast<arrow::DoubleArray>(taken);
        if (!values) {
            continue;
        }

        auto& fieldSeries = batchSeries[field];
        size_t offset = 0;
        for (const auto& symbol : effectiveSymbols) {
            const auto& rows = rowsBySymbol[symbol];
            auto& valuesForSymbol = fieldSeries[symbol];
            valuesForSymbol.reserve(rows.size());
            for (size_t rowOffset = 0; rowOffset < rows.size(); ++rowOffset) {
                const int64_t valueIndex = static_cast<int64_t>(offset + rowOffset);
                if (valueIndex >= values->length() || values->IsNull(valueIndex)) {
                    continue;
                }
                valuesForSymbol.push_back(values->Value(valueIndex));
            }
            offset += rows.size();
        }
    }

    return batchSeries;
}

int ArrowMarketData::symbolIndex(const std::string& symbol) const
{
    const auto it = symbolToIndex_.find(symbol);
    return it == symbolToIndex_.end() ? -1 : it->second;
}

int ArrowMarketData::dateIndex(const std::string& date) const
{
    const auto it = dateToIndex_.find(date);
    return it == dateToIndex_.end() ? -1 : it->second;
}

} // namespace factor