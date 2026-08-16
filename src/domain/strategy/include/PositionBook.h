#pragma once
// PositionBook — 内部持仓账本 (ADR-005 P5 簿记校验数据源, P1 落地)
// 账本随订单即时入账 (applyOrder 内存+dirty, 不逐单落盘); 与券商快照对账 (澄清 C3 在途豁免);
// flush() 落盘 app_state.json positionBook.<strategyId>.<period> (AppStateStore 原子写)
// adopt 双分支 (§9): 键不存在→采纳券商快照; 键存在(即使载荷损坏)→不采纳, 直接 P5 校验

#include "EvalTypes.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace astock::infrastructure::database { class AppStateStore; }

namespace domain::strategy {

/// @brief 未确认成交订单 (C3: 豁免 = 账本−快照 == Σ在途股数, 锚定提交日)
struct PendingFill {
    std::string symbol;
    std::int64_t quantity{0};   // 正=买入在途, 负=卖出在途
    std::int64_t submitDay{0};  // 提交日 YYYYMMDD (C3 豁免锚点: 提交日而非评估日)
};

/// @brief 内部持仓账本 (单写者: 引擎专用线程; flush 经 AppStateStore 互斥落盘)
class PositionBook {
public:
    /// @brief P5 对账结果
    struct CheckResult {
        bool ok{true};
        std::vector<std::string> mismatches;  // 明细: "sym 账本=.. 券商=.. 在途=.."
    };

    /// @param strategyId 策略 ID (持久化键组成, ADR-001 逻辑隔离)
    /// @param period 评估周期 (持久化键维度)
    /// @param store app_state.json 统一写者 (与调度器/同步服务同路径互斥)
    PositionBook(std::string strategyId, BarPeriod period,
                 std::shared_ptr<astock::infrastructure::database::AppStateStore> store);
    ~PositionBook();

    /// @brief 订单即时入账 (内存+dirty; 盘后由 flush 统一落盘)
    /// @param quantity 正=买入加仓, 负=卖出减仓
    /// @param submitDay 订单提交日 YYYYMMDD (C3 豁免锚点)
    void applyOrder(const std::string& symbol, std::int64_t quantity, std::int64_t submitDay);

    /// @brief 与券商快照对账 (C3 口径):
    /// 偏差==Σ在途 → 豁免; 在途已(部分)在快照体现 → 该部分转正式 (在途缩减, dirty);
    /// 其余任何偏差 (含快照多出账本没有的持仓) → mismatch
    [[nodiscard]] CheckResult checkAgainstBroker(const std::map<std::string, std::int64_t>& brokerSnapshot);

    /// @brief 首启采纳券商快照 (adopt 双分支: 仅当 app_state.json 无本账本键时调用)
    /// @return true=已采纳 (键不存在), false=键已存在不采纳 (直接走 checkAgainstBroker)
    [[nodiscard]] bool adoptBrokerSnapshotIfAbsent(const std::map<std::string, std::int64_t>& brokerSnapshot);

    /// @brief 落盘账本+在途 (优雅关闭必调; dirty 为 false 时 no-op)
    void flush();

    /// @brief 账本持仓只读副本
    [[nodiscard]] std::map<std::string, std::int64_t> positions() const { return m_positions; }

    /// @brief 在途订单只读副本 (P5 归因明细用)
    [[nodiscard]] const std::vector<PendingFill>& pendingFills() const noexcept { return m_pendingFills; }

private:
    /// @brief 持久化键: positionBook.<strategyId>.<period后缀>
    std::string evalKey() const { return m_strategyId + "." + BarPeriodNaming::suffix(m_period); }

    /// @brief 载荷序列化 (v1 格式: "v1|<账本段>|<在途段>")
    std::string serialize() const;
    /// @brief 载荷反序列化; 版本不符/解析失败 → false (键存在但载荷损坏, 不采纳券商快照)
    bool deserialize(const std::string& payload);

    /// @brief 将某标的在途订单按提交序(FIFO)缩减至剩余股数 == targetDelta
    /// 已转正式部分从 m_pendingFills 移除, 置 dirty
    void trimPendingToDelta(const std::string& symbol, std::int64_t targetDelta);

    std::string m_strategyId;
    BarPeriod m_period;
    std::shared_ptr<astock::infrastructure::database::AppStateStore> m_store;
    std::map<std::string, std::int64_t> m_positions;  // 账本: sym → 股数 (0 持仓不保留)
    std::vector<PendingFill> m_pendingFills;          // 在途订单 (C3)
    bool m_dirty{false};
};

} // namespace domain::strategy
