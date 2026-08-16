#pragma once
// IPriceProvider — EOD 评估当日价量数据源抽象 (P0 基础件)
// 职责: 按评估日取标的当日 close/volume/preClose, 支撑回退链装配
// 工厂是唯一周期/模式分支点 (ADR-009②), Facade 零周期分支

#include "EvalTypes.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace domain::strategy {

class IPriceProvider {
public:
    virtual ~IPriceProvider() = default;

    /// @brief 获取指定标的在评估日的日频价量
    /// @param symbols    参与评估的标的全码
    /// @param endDateStr 评估日 (YYYY-MM-DD)
    /// @param period     评估周期 (数据源按需忽略, 接口签名统一携带)
    /// @return 有数据的标的 → 当日价量; 空 map 表示该源无任何数据
    virtual std::map<std::string, EodDayBar> fetchPrices(
        const std::vector<std::string>& symbols,
        const std::string& endDateStr,
        BarPeriod period) const = 0;

    /// @brief 数据源名称 (回退链日志与诊断)
    virtual const char* sourceName() const noexcept = 0;
};

/// @brief 价格源装配工厂
class PriceProviderFactory {
public:
    /// @brief 按评估周期与模式装配价格源
    /// Daily+Intraday     → Fallback{tick缓存 → LiveData} (澄清 C10: DB 不参与盘中兜底)
    /// Daily+Compensation → DB 单级 (回看历史; 不静默: 空即 PriceDataEmpty)
    /// Minute1/5          → 接口位 (数据源未实现, 返回 nullptr)
    static std::unique_ptr<IPriceProvider> createProvider(BarPeriod period, EvalMode mode);
};

} // namespace domain::strategy
