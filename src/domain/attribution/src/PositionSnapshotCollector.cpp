#include "PositionSnapshotCollector.h"

#include <utility>

namespace domain::attribution {

void PositionSnapshotCollector::recordDay(const domain::DomainDate& date, double equity, const std::vector<Entry>& entries)
{
    // 同日重复调用（如调仓日两段市值循环）→ 覆盖为最后一次（当日收盘态）
    if (!m_snapshots.empty() && m_snapshots.back().date == date) {
        m_snapshots.back().equity = equity;
        m_snapshots.back().entries = entries;
        return;
    }
    m_snapshots.push_back(DaySnapshot{date, equity, entries});
}

void PositionSnapshotCollector::recordDay(const domain::DomainDate& date, double equity, std::vector<Entry>&& entries)
{
    if (!m_snapshots.empty() && m_snapshots.back().date == date) {
        m_snapshots.back().equity = equity;
        m_snapshots.back().entries = std::move(entries);
        return;
    }
    DaySnapshot snap;
    snap.date = date;
    snap.equity = equity;
    snap.entries = std::move(entries);
    m_snapshots.push_back(std::move(snap));
}

} // namespace domain::attribution
