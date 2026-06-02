#pragma once

#include "../../factor/include/factor_compute/FactorSignalTypes.h"

#include <vector>

namespace domain::strategy {

/// @brief P3-T1：策略信号批接口
///
/// 实施任务清单要求：
/// - 策略侧实现 evaluate_batch(time_window, instruments) 等价接口
/// - 批接口稳定，不走逐日回调主路径
/// - 输出为统一 SignalSet
///
/// 约束（落地设计 Section 4.2）：
/// - 策略信号通过统一 ISignalProducer 接入后半链
/// - 因子/策略共用同一后半链实现，无来源特判分支
class IStrategySignalEngine {
public:
    virtual ~IStrategySignalEngine() = default;

    /// @brief 批量评估策略信号
    ///
    /// 对指定时间窗口内的所有标的批量计算策略信号值，
    /// 返回统一 SignalSet 结构。
    ///
    /// @param dateRange 时间窗口的范围
    /// @param universe 标的池
    /// @return 统一 SignalSet，包含策略产生的所有信号值
    [[nodiscard]] virtual factor::compute::FactorResult<factor::compute::SignalSet>
    evaluateBatch(
        factor::compute::DateRange dateRange,
        const std::vector<factor::compute::InstrumentId>& universe) = 0;
};

} // namespace domain::strategy