#pragma once
// SurvivorshipValidator — 回测数据集幸存者偏差诊断工具
// 职责: 检测回测 universe 中是否存在幸存者偏差(仅含当前 ACTIVE 股票, 缺失已退市股)
// 纯 C++17, 零 Qt 依赖
// 设计: 接受预加载的 SymbolLifecycle 数据, 不持有数据库连接

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace domain::backtest {

/// @brief 单只股票的生命周期信息 (从 ref.symbol_info 批量加载)
struct SymbolLifecycle {
    std::string symbol;
    int listDate{0};      // YYYYMMDD, 0=未知
    int delistDate{0};    // YYYYMMDD, 0=未退市(至今仍活跃)
    std::string status;   // ACTIVE/DELISTED/..., 仅用于日志, 不用于历史日期判断

    /// @brief 在 targetDate 当天是否活跃(listDate <= targetDate < delistDate)
    [[nodiscard]] bool isActiveOn(int targetDate) const noexcept {
        if (listDate <= 0) return true;   // 未知上市日期 → 假设一直活跃
        if (targetDate < listDate) return false;
        if (delistDate > 0 && targetDate >= delistDate) return false;
        return true;
    }
};

/// @brief 单项验证检查结果
struct SurvivorshipCheckResult {
    bool passed{true};
    std::string checkName;                    // 检查名称
    std::string detail;                       // 人类可读摘要
    std::vector<std::string> violations;      // 违规详情 (最多保留 kMaxViolations 条)
    int totalViolations{0};                   // 违规总数 (可能超过 violations.size())

    static constexpr int kMaxViolations = 20; // 单检查最多保留的违规条目数
};

/// @brief 完整验证报告
struct SurvivorshipValidationReport {
    std::string backtestStart;
    std::string backtestEnd;
    int totalDates{0};
    int totalStocks{0};
    std::vector<SurvivorshipCheckResult> checks;

    [[nodiscard]] bool allPassed() const noexcept {
        for (const auto& c : checks) {
            if (!c.passed) return false;
        }
        return true;
    }

    /// @brief 人类可读摘要 (单行)
    [[nodiscard]] std::string summary() const;
};

/// @brief 幸存者偏差验证器
///
/// 使用方式:
///   1. 从 PG 批量加载 SymbolLifecycle 数据
///   2. 构造 SurvivorshipValidator, 调用 preload()
///   3. 调用 validate() 获取报告
///
/// 不持有数据库连接, 数据由调用方注入.
class SurvivorshipValidator {
public:
    SurvivorshipValidator() = default;

    /// @brief 预加载全市场股票的生命周期数据 (调用方从 ref.symbol_info 批量查询后注入)
    /// @param lifecycles 所有相关股票的 SymbolLifecycle 列表
    /// @param backtestStart 回测起始日期 (YYYYMMDD int)
    /// @param backtestEnd   回测结束日期 (YYYYMMDD int)
    void preload(std::vector<SymbolLifecycle> lifecycles,
                 int backtestStart,
                 int backtestEnd);

    /// @brief 执行全量验证, 返回报告
    /// @param universe 回测 universe 中出现的所有 symbol
    /// @param dates    回测的交易日列表 (YYYYMMDD int, 已排序)
    /// @param universeByDate 可选: date→symbols, 若提供则执行逐日检查; 否则仅做全量检查
    [[nodiscard]] SurvivorshipValidationReport validate(
        const std::vector<std::string>& universe,
        const std::vector<int>& dates,
        const std::unordered_map<int, std::vector<std::string>>* universeByDate = nullptr) const;

private:
    /// @brief 检查1: 回测区间内是否有退市事件, 若有但 universe 中缺失退市股 → 幸存者偏差红旗
    [[nodiscard]] SurvivorshipCheckResult checkActiveOnlyBias(
        const std::vector<std::string>& universe) const;

    /// @brief 检查2: universe 中每只股票在回测首日前必须已上市
    [[nodiscard]] SurvivorshipCheckResult checkListedBeforeStart(
        const std::vector<std::string>& universe) const;

    /// @brief 检查3: 分层抽样 — 均匀间隔取 N 个日期, 确认当天应有数据的股票确实在 universe 中
    [[nodiscard]] SurvivorshipCheckResult checkSpotCheckByDate(
        const std::vector<int>& dates,
        int sampleCount) const;

    /// @brief 检查4: 退市股在退市日后不应出现在 universe 中 (仅当提供了 universeByDate)
    [[nodiscard]] SurvivorshipCheckResult checkDelistedExcluded(
        const std::unordered_map<int, std::vector<std::string>>& universeByDate) const;

    // 预加载的数据
    std::unordered_map<std::string, SymbolLifecycle> m_lifecycleBySymbol;
    // 回测区间内发生的退市事件 (delistDate 在 [start, end] 内的 symbol)
    std::vector<std::string> m_delistedInRange;
    bool m_hasDelistingsInRange{false};
    int m_backtestStart{0};
    int m_backtestEnd{0};
};

} // namespace domain::backtest
