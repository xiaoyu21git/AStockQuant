#pragma once

#include "../../../engine/include/Event/EventFormat.hpp"
#include "../../../engine/include/Event/EventBus.hpp"
#include "foundation/Utils/Uuid.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace domain::strategy {

/// @brief 风控事件订阅器
///
/// 订阅 EventBus 上的 news.* 事件，根据事件内容动态调整风控参数：
///   - 立案调查 → 封禁开仓 + 收紧止损 (-10% → -5%)
///   - ST 警示   → 封禁开仓 + 降仓位上限 (10% → 2%)
///   - 政策负面  → 降总敞口 (100% → 80%)
///
/// 生命周期: AppBootstrap 启动时创建，全局单例
/// 解禁逻辑: T+1 自动解禁 (evaluateEndOfDay 入口清空)
class EventRiskSubscriber final {
public:
    static EventRiskSubscriber& instance();

    EventRiskSubscriber();
    ~EventRiskSubscriber();

    // ── 生命周期 ──
    bool isStarted() const { return m_bus != nullptr; }
    void start();
    void stop();

    // ── 封禁名单查询 ──
    /// @brief 因事件被临时禁止开仓的标的代码 (不含后缀)
    const std::unordered_set<std::string>& blockedSymbols() const {
        return m_blockedSymbols;
    }

    /// @brief T+1 解禁: 每交易日清空临时风控限制
    void clearBlockedSymbols() {
        m_blockedSymbols.clear();
        m_liquidatedSymbols.clear();
        m_sectorLimits.clear();
    }

    /// @brief 需要立即清仓的标的 (由 RiskManager 巡检消费)
    const std::unordered_set<std::string>& liquidatedSymbols() const {
        return m_liquidatedSymbols;
    }

private:
    // ── EventBus 回调 ──
    void onFinancialEvent(const engine::EventFormat& event);

    /// @brief 收紧单只标的持仓上限
    void tightenSingleSymbolLimit(const std::string& symbol, double maxPct);

    /// @brief 限制行业敞口
    void capSectorExposure(const std::string& sectorCodes,
                          const std::string& productId,
                          double maxPct);

    /// @brief 根据事件标签执行风控动作 (原有方法)
    void applyEventTags(const std::string& eventType,
                        const std::string& symbol,
                        const std::unordered_map<std::string, std::string>& tags,
                        double sentiment,
                        double confidence);

    engine::EventBus* m_bus = nullptr;
    foundation::utils::Uuid m_subscriptionId;

    // 因事件被临时封禁的标的 (T+1 清空)
    std::unordered_set<std::string> m_blockedSymbols;
    // 需要立即清仓的标的
    std::unordered_set<std::string> m_liquidatedSymbols;
    // 行业临时限制: sectorCode → maxPositionPct
    std::unordered_map<std::string, double> m_sectorLimits;

    // 备份原始风控配置 (用于事件过期后恢复, 预留)
    struct PerSymbolOverride {
        double stopLossPct = 0.0;
        double positionLimitPct = 0.0;
    };
    std::unordered_map<std::string, PerSymbolOverride> m_overrides;
};

} // namespace domain::strategy
