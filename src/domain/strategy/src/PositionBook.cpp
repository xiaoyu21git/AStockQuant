#include "PositionBook.h"
#include "../../../infrastructure/include/database/AppStateStore.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
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
    if (!m_store || m_strategyId.empty()) return;  // 无存储 → 纯内存账本 (测试路径)

    std::string payload;
    if (m_store->readString("positionBook", evalKey(), payload)) {
        if (!deserialize(payload)) {
            // 键存在但载荷损坏 → 待首个非空券商快照重建账本 (不再按空处理报偏差)
            INTERNAL_ERROR_STREAM << "[PositionBook] 载荷损坏, 待券商快照重建账本";
            m_pendingAdoption = true;
        } else {
            INTERNAL_INFO_STREAM << "[PositionBook] 加载账本: " << m_positions.size()
                                 << " 项持仓, " << m_pendingFills.size() << " 笔在途";
        }
    } else {
        // 键不存在 (首次运行) → 待首个非空券商快照采纳
        m_pendingAdoption = true;
    }
}

PositionBook::~PositionBook() {
    flush();  // 析构兜底落盘 (正常路径优雅关闭前已 flush, dirty=false 时 no-op)
}

void PositionBook::applyOrder(const std::string& symbol, std::int64_t quantity,
                              std::int64_t submitDay) {
    if (symbol.empty() || quantity == 0) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    // 账本即时入账 (账本含订单效果, 与券商快照的偏差即"未确认成交"部分)
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

void PositionBook::reconcileToBroker(const std::unordered_map<std::string, std::int64_t>& brokerSnapshot) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // ── 待采纳 (键缺失/载荷损坏): 首个非空快照整体采纳, 与首启语义一致 ──
    if (m_pendingAdoption) {
        const bool hasAny = std::any_of(brokerSnapshot.begin(), brokerSnapshot.end(),
                                        [](const auto& kv) { return kv.second != 0; });
        if (!hasAny) return;  // 空/全零快照不可信, 等下一次推送
        m_positions.clear();
        for (const auto& [sym, qty] : brokerSnapshot)
            if (qty != 0) m_positions[sym] = qty;
        m_pendingFills.clear();
        m_pendingAdoption = false;
        m_dirty = true;
        INTERNAL_INFO_STREAM << "[PositionBook] 账本采纳券商快照: " << m_positions.size() << " 项持仓";
        return;
    }

    std::map<std::string, std::int64_t> pendingSum;
    for (const auto& f : m_pendingFills) pendingSum[f.symbol] += f.quantity;

    // 键集合: 账本 ∪ 券商快照
    std::set<std::string> syms;
    for (const auto& [sym, qty] : m_positions) (void)qty, syms.insert(sym);
    for (const auto& [sym, qty] : brokerSnapshot) (void)qty, syms.insert(sym);

    for (const auto& sym : syms) {
        const auto bookIt = m_positions.find(sym);
        const auto snapIt = brokerSnapshot.find(sym);
        const std::int64_t book = bookIt != m_positions.end() ? bookIt->second : 0;
        const std::int64_t snap = snapIt != brokerSnapshot.end() ? snapIt->second : 0;
        const std::int64_t inFlight = pendingSum[sym];
        const std::int64_t delta = book - snap;

        if (delta == 0 || delta == inFlight) {  // 一致 / 偏差全归因于在途订单 → 等券商成交
            m_confirm.erase(sym);
            continue;
        }

        // 在途已(部分)在券商快照体现 → 该部分转正式 (在途缩减至剩余偏差, FIFO)
        const bool confirmedSome =
            (inFlight != 0 && (delta > 0) == (inFlight > 0)
             && std::llabs(delta) < std::llabs(inFlight));
        if (confirmedSome) {
            trimPendingToDelta(sym, delta);
            m_confirm.erase(sym);
            continue;
        }

        // 剩余偏差 (用户手动交易): 同一券商值连续 kReconcileConfirmations 次确认后对齐,
        // 防单次快照抖动
        auto& c = m_confirm[sym];
        if (c.first != snap) { c = {snap, 1}; continue; }
        if (++c.second < kReconcileConfirmations) continue;
        m_confirm.erase(sym);

        if (bookIt != m_positions.end()) {
            // 账本持有 → 对齐券商 (用户手动卖出跟随; 在途订单保留, 成交后自然回补)
            if (snap > 0) m_positions[sym] = snap;
            else m_positions.erase(sym);
            m_dirty = true;
            INTERNAL_WARN_STREAM << "[PositionBook] 账本对齐券商: " << sym
                                 << " 账本=" << book << " → 券商=" << snap
                                 << " (连续 " << kReconcileConfirmations << " 次快照确认)";
        } else if (snap != 0 && inFlight != 0 && snap == inFlight) {
            // 账本无、券商有、与在途股数完全吻合 → 引擎订单成交补记 (账本先对齐后才成交的窗口)
            m_positions[sym] = snap;
            m_pendingFills.erase(
                std::remove_if(m_pendingFills.begin(), m_pendingFills.end(),
                               [&sym](const PendingFill& f) { return f.symbol == sym; }),
                m_pendingFills.end());
            m_dirty = true;
            INTERNAL_INFO_STREAM << "[PositionBook] 在途订单成交补记: " << sym << " " << snap << " 股";
        }
        // 其余券商有、账本无 (用户手动买入) → 不入账本, 策略不替用户管理
    }
}

void PositionBook::flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_dirty) return;
    m_dirty = false;
    if (!m_store || m_strategyId.empty()) return;
    m_store->writeString("positionBook", evalKey(), serialize());
}

std::set<std::string> PositionBook::heldCodes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::set<std::string> out;
    for (const auto& [sym, qty] : m_positions)
        if (qty != 0) out.insert(sym);
    return out;
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
