#include "PositionBook.h"
#include "../../../infrastructure/include/database/AppStateStore.h"
#include "foundation/log/logging.hpp"

#include <cstdlib>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace domain::strategy {

PositionBook::PositionBook(std::string strategyId, BarPeriod period,
                           std::shared_ptr<astock::infrastructure::database::AppStateStore> store)
    : m_strategyId(std::move(strategyId)), m_period(period), m_store(std::move(store))
{
    if (!m_store || m_strategyId.empty()) return;

    std::string payload;
    if (m_store->readString("positionBook", evalKey(), payload)) {
        if (!deserialize(payload)) {
            // 键存在但载荷损坏 → 不采纳券商快照 (adopt 双分支), 账本按空处理 → P5 校验报偏差人工介入 (保守截断)
            INTERNAL_ERROR_STREAM << "[PositionBook] 载荷损坏, 账本按空处理 "
                << "(键已存在 → 不采纳券商快照, P5 校验将报 BookKeepingMismatch)";
        } else {
            INTERNAL_INFO_STREAM << "[PositionBook] 加载账本: " << m_positions.size()
                                 << " 项持仓, " << m_pendingFills.size() << " 笔在途";
        }
    }
}

PositionBook::~PositionBook() {
    flush();  // 析构兜底落盘 (正常路径优雅关闭前已 flush, dirty=false 时 no-op)
}

void PositionBook::applyOrder(const std::string& symbol, std::int64_t quantity,
                              std::int64_t submitDay) {
    if (symbol.empty() || quantity == 0) return;

    // 账本即时入账 (C3: 账本含订单效果, 与券商快照的偏差即"未确认成交"部分)
    auto it = m_positions.find(symbol);
    const std::int64_t newQty = (it != m_positions.end() ? it->second : 0) + quantity;
    if (newQty == 0) {
        if (it != m_positions.end()) m_positions.erase(it);
    } else {
        m_positions[symbol] = newQty;
    }

    m_pendingFills.push_back(PendingFill{symbol, quantity, submitDay});
    m_dirty = true;
}

PositionBook::CheckResult PositionBook::checkAgainstBroker(
    const std::map<std::string, std::int64_t>& brokerSnapshot)
{
    CheckResult res;

    std::map<std::string, std::int64_t> pendingSum;
    for (const auto& f : m_pendingFills) pendingSum[f.symbol] += f.quantity;

    // 键集合: 账本 ∪ 券商快照 ∪ 在途
    std::set<std::string> syms;
    for (const auto& [sym, qty] : m_positions) (void)qty, syms.insert(sym);
    for (const auto& [sym, qty] : brokerSnapshot) (void)qty, syms.insert(sym);
    for (const auto& [sym, qty] : pendingSum) (void)qty, syms.insert(sym);

    for (const auto& sym : syms) {
        const auto bookIt = m_positions.find(sym);
        const auto snapIt = brokerSnapshot.find(sym);
        const std::int64_t book = bookIt != m_positions.end() ? bookIt->second : 0;
        const std::int64_t snap = snapIt != brokerSnapshot.end() ? snapIt->second : 0;
        const std::int64_t inFlight = pendingSum[sym];
        const std::int64_t delta = book - snap;

        // C3: 偏差完全归因于在途订单 → 豁免
        if (delta == inFlight) continue;

        // C3: 在途已(部分)在券商快照体现 → 该部分转正式 (在途缩减至剩余偏差, FIFO)
        const bool confirmedSome =
            (delta == 0 && inFlight != 0)
            || (inFlight != 0 && (delta > 0) == (inFlight > 0)
                && std::llabs(delta) < std::llabs(inFlight));
        if (confirmedSome) {
            trimPendingToDelta(sym, delta);
            continue;
        }

        // 其余偏差 (含快照多出账本没有的持仓) → BookKeepingMismatch 明细
        res.ok = false;
        res.mismatches.push_back(sym + " 账本=" + std::to_string(book)
            + " 券商=" + std::to_string(snap) + " 在途=" + std::to_string(inFlight));
    }
    return res;
}

bool PositionBook::adoptBrokerSnapshotIfAbsent(
    const std::map<std::string, std::int64_t>& brokerSnapshot)
{
    if (!m_store || m_strategyId.empty()) return false;
    // adopt 双分支 (§9): 键存在(即使映射为空/载荷损坏) → 不采纳, 直接 P5 校验
    if (m_store->hasKey("positionBook", evalKey())) return false;

    m_positions.clear();
    for (const auto& [sym, qty] : brokerSnapshot)
        if (qty != 0) m_positions[sym] = qty;
    m_pendingFills.clear();
    m_dirty = true;
    INTERNAL_INFO_STREAM << "[PositionBook] 首启采纳券商快照: " << m_positions.size() << " 项持仓";
    flush();
    return true;
}

void PositionBook::flush() {
    if (!m_dirty) return;
    m_dirty = false;
    if (!m_store || m_strategyId.empty()) return;
    m_store->writeString("positionBook", evalKey(), serialize());
}

void PositionBook::trimPendingToDelta(const std::string& symbol, std::int64_t targetDelta) {
    // 按提交序(FIFO)缩减在途: 券商先确认先提交的订单; 剩余在途股数和 == targetDelta
    std::vector<PendingFill> kept;
    std::int64_t keptSum = 0;
    for (auto& f : m_pendingFills) {
        if (f.symbol != symbol) {
            kept.push_back(f);
            continue;
        }
        if (keptSum == targetDelta) continue;  // 本笔及之后全部转正式
        const std::int64_t need = targetDelta - keptSum;
        if (std::llabs(f.quantity) <= std::llabs(need)) {
            kept.push_back(f);  // 整笔仍在途
            keptSum += f.quantity;
        } else {
            f.quantity = need;  // 部分在途 (其余转正式)
            kept.push_back(f);
            keptSum += need;
        }
    }
    m_pendingFills = std::move(kept);
    m_dirty = true;
}

// ═══════════════════════════════════════════════════════════════════
// 载荷序列化 (v1 格式, 单写者 PositionBook, 内部格式不对外)
// ═══════════════════════════════════════════════════════════════════

std::string PositionBook::serialize() const {
    std::string out = "v1|";
    bool first = true;
    for (const auto& [sym, qty] : m_positions) {
        if (!first) out += ';';
        first = false;
        out += sym;
        out += ':';
        out += std::to_string(qty);
    }
    out += '|';
    first = true;
    for (const auto& f : m_pendingFills) {
        if (!first) out += ';';
        first = false;
        out += f.symbol;
        out += ':';
        out += std::to_string(f.quantity);
        out += ':';
        out += std::to_string(f.submitDay);
    }
    return out;
}

bool PositionBook::deserialize(const std::string& payload) {
    // 按分隔符切段 (保留空段: "v1||" 合法, 表示空账本+空在途)
    auto splitBy = [](const std::string& s, char sep, std::vector<std::string>& out) {
        std::size_t start = 0;
        while (start <= s.size()) {
            const auto pos = s.find(sep, start);
            out.push_back(s.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
            if (pos == std::string::npos) break;
            start = pos + 1;
        }
    };

    std::vector<std::string> parts;
    splitBy(payload, '|', parts);
    if (parts.size() != 3 || parts[0] != "v1") return false;

    auto parsePosSegment = [this, &splitBy](const std::string& seg) {
        if (seg.empty()) return true;
        std::vector<std::string> entries;
        splitBy(seg, ';', entries);
        for (const auto& e : entries) {
            if (e.empty()) continue;
            std::vector<std::string> fields;
            splitBy(e, ':', fields);
            if (fields.size() != 2) return false;
            m_positions[fields[0]] = std::stoll(fields[1]);
        }
        return true;
    };
    auto parsePendingSegment = [this, &splitBy](const std::string& seg) {
        if (seg.empty()) return true;
        std::vector<std::string> entries;
        splitBy(seg, ';', entries);
        for (const auto& e : entries) {
            if (e.empty()) continue;
            std::vector<std::string> fields;
            splitBy(e, ':', fields);
            if (fields.size() != 3) return false;
            m_pendingFills.push_back(PendingFill{fields[0], std::stoll(fields[1]), std::stoll(fields[2])});
        }
        return true;
    };

    try {
        if (!parsePosSegment(parts[1])) return false;
        if (!parsePendingSegment(parts[2])) return false;
    } catch (...) {
        return false;
    }
    return true;
}

} // namespace domain::strategy
