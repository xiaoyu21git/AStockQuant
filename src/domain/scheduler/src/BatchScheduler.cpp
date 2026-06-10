#include "../include/BatchScheduler.h"

#include <algorithm>
#include <cstddef>

namespace domain::scheduler {

void BatchScheduler::loadTradingCalendar(
    factor::compute::DateKey start,
    factor::compute::DateKey end,
    std::vector<factor::compute::DateKey>& tradingDates) const
{
    tradingDates.clear();

    if (!start.isValid() || !end.isValid() || start.value > end.value) {
        return;
    }

    // 框架占位：实际实现需要从数据库交易日历表读取
    // 当前按自然日生成，后续替换为真实交易日历
    const int32_t startVal = std::max(start.value, factor::compute::DateKey::kMinimumDate);
    const int32_t endVal = std::min(end.value, factor::compute::DateKey::kMaximumDate);

    // 预留容量（估算约 250 个交易日/年）
    const int32_t estimatedDays = (endVal - startVal) / 10000 * 250;
    if (estimatedDays > 0) {
        tradingDates.reserve(static_cast<std::size_t>(estimatedDays));
    }

    // 模拟：按 yyyymmdd 自增遍历
    auto ymdToDays = [](int32_t ymd) -> int32_t {
        int32_t y = ymd / 10000;
        int32_t m = (ymd % 10000) / 100;
        int32_t d = ymd % 100;
        return y * 365 + y / 4 - y / 100 + y / 400 + (m - 1) * 30 + d;
    };

    for (int32_t d = startVal; d <= endVal; ++d) {
        factor::compute::DateKey key;
        key.value = d;
        tradingDates.push_back(key);
    }
}

std::vector<StockBatch> BatchScheduler::splitStockBatches(
    const std::vector<factor::compute::InstrumentId>& universe,
    std::size_t batchSize) const
{
    std::vector<StockBatch> batches;

    if (universe.empty() || batchSize == 0U) {
        return batches;
    }

    const std::size_t total = universe.size();
    const std::size_t batchCount = (total + batchSize - 1U) / batchSize;
    batches.reserve(batchCount);

    for (std::size_t i = 0U; i < total; i += batchSize) {
        StockBatch batch;
        batch.batchIndex = i / batchSize;
        const std::size_t end = (std::min)(i + batchSize, total);
        batch.instruments.assign(universe.begin() + static_cast<std::ptrdiff_t>(i),
                                  universe.begin() + static_cast<std::ptrdiff_t>(end));
        batches.push_back(std::move(batch));
    }

    return batches;
}

} // namespace domain::scheduler