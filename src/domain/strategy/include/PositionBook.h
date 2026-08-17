#pragma once
// PositionBook — 内部持仓账本 (ADR-005, P1 落地)
// 账本随订单即时入账 (applyOrder 内存+dirty, 不逐单落盘); 实时对账 (reconcileToBroker):
// 券商快照连续 kReconcileConfirmations 次确认同一值 → 账本对齐券商 (在途订单豁免);
// 对账只修正不拦截, 下单流程与账实一致性零耦合 (ADR-005 修订)
// flush() 落盘 app_state.json positionBook.<strategyId>.<period> (AppStateStore 原子写)
// 初始化规则: 键不存在/载荷损坏 → 首个非空券商快照整体采纳为账本

#include "EvalTypes.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace astock::infrastructure::database { class AppStateStore; }

namespace domain::strategy {

/// @brief 未确认成交订单 (豁免口径: 账本−快照偏差 == Σ在途股数, 锚定提交日)
struct PendingFill {
    std::string symbol;
    std::int64_t quantity{0};   // 正=买入在途, 负=卖出在途
    std::int64_t submitDay{0};  // 提交日 YYYYMMDD (豁免锚点: 提交日而非评估日)
};

/// @brief 内部持仓账本 (多线程: applyOrder=引擎线程, reconcileToBroker=GM回调线程,
///        flush=引擎线程; 内部互斥, 落盘经 AppStateStore 文件互斥)
class PositionBook {
public:
    /// @param strategyId 策略 ID (持久化键组成, ADR-001 逻辑隔离)
    /// @param period 评估周期 (持久化键维度)
    /// @param store app_state.json 统一写者 (与调度器/同步服务同路径互斥)
    PositionBook(std::string strategyId, BarPeriod period,
                 std::shared_ptr<astock::infrastructure::database::AppStateStore> store);
    ~PositionBook();

    /// @brief 订单即时入账 (内存+dirty; 盘后由 flush 统一落盘)
    /// @param quantity 正=买入加仓, 负=卖出减仓
    /// @param submitDay 订单提交日 YYYYMMDD (在途豁免锚点)
    void applyOrder(const std::string& symbol, std::int64_t quantity, std::int64_t submitDay);

    /// @brief 实时对账 (券商快照推送驱动, 幂等):
    ///   - 待采纳 (键缺失/载荷损坏) → 首个非空快照整体采纳为账本
    ///   - 账本中标的与快照偏差且不归因于在途 → 同一券商值连续 kReconcileConfirmations
    ///     次确认后对齐 (用户手动卖出跟随; 手动买入不入账本, 例外: 与在途订单股数
    ///     完全吻合 → 引擎订单成交补记)
    ///   - 不产生失败状态: 对账只修正不拦截
    void reconcileToBroker(const std::unordered_map<std::string, std::int64_t>& brokerSnapshot);

    /// @brief 落盘账本+在途 (优雅关闭必调; dirty 为 false 时 no-op)
    void flush();

    /// @brief 当前持仓纯代码集合快照 (线程安全; 供 live.current_position 策略归属判定)
    /// 语义: 账本中股数非零的标的 = 策略持仓; 券商有而账本无 = 手动持仓
    std::set<std::string> heldCodes() const;

private:
    /// @brief 持久化键: positionBook.<strategyId>.<period后缀>
    std::string evalKey() const { return m_strategyId + "." + BarPeriodNaming::suffix(m_period); }

    /// @brief 载荷序列化 (v1 格式: "v1|<账本段>|<在途段>")
    std::string serialize() const;
    /// @brief 载荷反序列化; 版本不符/解析失败 → false (调用方置待采纳)
    bool deserialize(const std::string& payload);

    /// @brief 将某标的在途订单按提交序(FIFO)缩减至剩余股数 == targetDelta (调用方持锁)
    void trimPendingToDelta(const std::string& symbol, std::int64_t targetDelta);

    std::string m_strategyId;
    BarPeriod m_period;
    std::shared_ptr<astock::infrastructure::database::AppStateStore> m_store;
    std::map<std::string, std::int64_t> m_positions;  // 账本: sym → 股数 (0 持仓不保留)
    std::vector<PendingFill> m_pendingFills;          // 在途订单 (豁免口径)
    bool m_dirty{false};
    bool m_pendingAdoption{false};                    // 键缺失/载荷损坏 → 待首个非空快照采纳

    // 对齐确认计数: sym → (上次券商快照值, 连续确认次数); 值变化重置, 达阈值对齐后清除
    std::unordered_map<std::string, std::pair<std::int64_t, int>> m_confirm;

    mutable std::mutex m_mutex;  // 多线程保护 (applyOrder/reconcileToBroker/flush/heldCodes)

    static constexpr int kReconcileConfirmations = 2;  // 连续快照确认次数 (防单次接口抖动)
};

} // namespace domain::strategy
