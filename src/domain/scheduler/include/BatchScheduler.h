#pragma once

#include "../../factor/include/factor_compute/FactorSignalTypes.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace domain::scheduler {

/// @brief 标的分批结果
struct StockBatch final {
    /// 本批标的列表
    std::vector<factor::compute::InstrumentId> instruments;

    /// 批次序号（0-based）
    std::size_t batchIndex{0};
};

/// @brief 批次调度器
///
/// 负责：
/// 1. 加载交易日历
/// 2. 将全市场标的按固定大小拆分为多批
/// 3. 循环调度每批执行
///
/// 约束：
/// - 纯 C++，零 Qt 依赖
/// - 不包含业务计算逻辑，只负责调度编排
/// - 集成 ResourceGovernor 资源检查
class BatchScheduler final {
public:
    /// 默认每批标的数量
    static constexpr std::size_t kDefaultBatchSize = 500U;

    BatchScheduler() = default;

    /// @brief 加载交易日历
    ///
    /// @param start  起始日期
    /// @param end    结束日期
    /// @param tradingDates  输出交易日列表
    void loadTradingCalendar(
        factor::compute::DateKey start,
        factor::compute::DateKey end,
        std::vector<factor::compute::DateKey>& tradingDates) const;

    /// @brief 按标的分批
    ///
    /// @param universe   全量标的池
    /// @param batchSize  每批大小
    /// @return           分批结果列表
    [[nodiscard]] std::vector<StockBatch> splitStockBatches(
        const std::vector<factor::compute::InstrumentId>& universe,
        std::size_t batchSize = kDefaultBatchSize) const;

    /// @brief 迭代每批，执行回调
    ///
    /// 模板方法，每批执行 fn(StockBatch)，
    /// 批次之间可插入资源检查。
    ///
    /// @param universe   全量标的池
    /// @param batchSize  每批大小
    /// @param fn         批次回调
    template <typename BatchFn>
    void forEachBatch(
        const std::vector<factor::compute::InstrumentId>& universe,
        std::size_t batchSize,
        BatchFn&& fn)
    {
        const std::vector<StockBatch> batches = splitStockBatches(universe, batchSize);
        for (const StockBatch& batch : batches) {
            fn(batch);
        }
    }
};

} // namespace domain::scheduler