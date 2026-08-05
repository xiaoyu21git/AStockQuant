#pragma once
// RuleAttribution — 独立归因收集器
// 不侵入 RuleTemplate / RuleGate, 由 StrategyEngine 主动推送事件
// 零 Qt, 纯 C++

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace factor { namespace compute { class IMarketDataView; } }

namespace domain::strategy::rules {

/// @brief 单模板归因汇总
struct RuleAttribution {
    int preventedTrades{0};              // 被封堵的买入信号数
    double preventedHypotheticalPnL{0.0}; // 若封堵未发生, 这些交易的假设 P&L (百分比累计)
    double preventedWinRate{0.0};        // 被封堵信号中本可盈利的比例 (0.0-1.0)
    int triggeredExits{0};              // 触发的出场次数
    double exitRealizedPnL{0.0};        // 出场交易的已实现 P&L (百分比累计)
    // 交易质量指标
    int topBoughtCount{0};     // 挑顶买入: 买入后5日跌 >5%
    int missedGainCount{0};    // 卖飞: 卖出后5日涨 >5%
    int stopLossCount{0};      // 止损出场次数 (规则触发, 非止损系统)
};

/// @brief 被封堵的买入信号记录
struct BlockedSignalRecord {
    std::string symbol;          // fullSymbol
    std::string templateId;      // 封堵此信号的模板 ID
    std::string ruleId;          // 封堵此信号的具体规则 ID
    double price{0.0};           // 封堵时价格
    int dayRow{-1};              // 在行情视图中的行号
};

/// @brief 规则触发的出场信号记录
struct ExitSignalRecord {
    std::string symbol;          // fullSymbol
    std::string templateId;      // 触发出场的模板 ID
    std::string ruleId;          // 触发出场的具体规则 ID
    double exitPrice{0.0};       // 出场价格
    double entryPrice{0.0};      // 入场价格
    double realizedPnL{0.0};     // 已实现盈亏 (百分比)
    int entryRow{-1};            // 入场行号
    int exitRow{-1};             // 出场行号
};

/// @brief 独立归因收集器 — 接收事件, 事后计算
/// 生命周期: 回测开始时创建, 回测结束后 compute() → results()
class AttributionCollector {
public:
    AttributionCollector() = default;

    /// @brief 记录一个被封堵的买入信号
    void recordBlocked(const BlockedSignalRecord& rec);

    /// @brief 记录一个规则触发的出场信号
    void recordExit(const ExitSignalRecord& rec);

    /// @brief 回测结束后调用: 遍历 blocked 记录, 用行情视图计算反事实盈亏
    /// @param view 行情视图 (需覆盖所有记录的 dayRow 及后续 N 日)
    /// @param forwardDays 反事实窗口 (默认 5 个交易日)
    void compute(const factor::compute::IMarketDataView* view, int forwardDays = 5);

    /// @brief 输出归因结果: templateId → 归因汇总
    [[nodiscard]] std::map<std::string, RuleAttribution> results() const;

    /// @brief 输出规则级归因: ruleId → 归因汇总
    [[nodiscard]] std::map<std::string, RuleAttribution> ruleResults() const;

    /// @brief 清空所有记录
    void clear();

    /// @brief 记录总数
    [[nodiscard]] std::size_t blockedCount() const noexcept { return m_blockedRecords.size(); }
    [[nodiscard]] std::size_t exitCount() const noexcept { return m_exitRecords.size(); }

private:
    std::vector<BlockedSignalRecord> m_blockedRecords;
    std::vector<ExitSignalRecord> m_exitRecords;
};

} // namespace domain::strategy::rules
