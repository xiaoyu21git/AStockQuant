#pragma once

#include "../../types/DomainDate.h"

#include <cstddef>
#include <string>
#include <vector>

namespace domain::attribution {

/// @brief 回测循环内轻量持仓快照采集器（仿 rules::AttributionCollector 模式）
///
/// 只采集不做计算，归因计算统一在 StrategyAttributionCalculator 回测后完成。
/// 内存有界：每日仅存 (date, equity, 持仓市值向量)，与持仓数同阶。
class PositionSnapshotCollector final {
public:
    /// @brief 单日快照条目（symbol → 当日收盘市值）
    struct Entry final {
        std::string symbol;
        double marketValue{0.0};
    };

    /// @brief 单日快照
    struct DaySnapshot final {
        domain::DomainDate date;
        double equity{0.0};
        std::vector<Entry> entries;
    };

    /// @brief 记录某日收盘持仓市值（同日重复调用覆盖为最后一次，保证收盘态）
    void recordDay(const domain::DomainDate& date, double equity, const std::vector<Entry>& entries);

    /// @brief 右值重载（采集点直接移动，避免拷贝）
    void recordDay(const domain::DomainDate& date, double equity, std::vector<Entry>&& entries);

    [[nodiscard]] const std::vector<DaySnapshot>& snapshots() const noexcept { return m_snapshots; }
    [[nodiscard]] std::size_t size() const noexcept { return m_snapshots.size(); }
    void clear() noexcept { m_snapshots.clear(); }

private:
    std::vector<DaySnapshot> m_snapshots;
};

} // namespace domain::attribution
