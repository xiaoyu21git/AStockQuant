#pragma once

#include "PositionSnapshotCollector.h"
#include "StrategyAttributionTypes.h"

#include "../../strategy/include/StrategySnapshotTypes.h"         // BacktestTradeRecord
#include "../../factor/include/factor_compute/IMarketDataView.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace domain::attribution {

/// @brief 策略归因计算器（行业 / 个股 / Brinson 三块，回测后统一计算）
///
/// 输入全部由外部注入（沿用旧 Config 注入哲学），零 Qt、零 DB。
/// 口径约定：
///  - 行业/个股：已实现盈亏（元，tradeLog 聚合）
///  - Brinson：对齐日集合（组合快照日 ∩ 基准权重日）上逐日算术累加
///    AE = Σ(w_p−w_b)·r_b，SE = Σ w_b·(r_p−r_b)，IE = Σ(w_p−w_b)·(r_p−r_b)
///  - 恒等式：|AE+SE+IE − Σ_{aligned}(R_p−R_b)| < kIdentityTolerance（对齐口径）
class StrategyAttributionCalculator final {
public:
    /// @brief 计算输入（引用在 compute 调用期间保持有效）
    struct Inputs final {
        const std::vector<domain::strategy::BacktestTradeRecord>* tradeLog{nullptr};
        const std::vector<PositionSnapshotCollector::DaySnapshot>* snapshots{nullptr};
        /// symbol → 行业码；未提供或返回空串 → 归入"未分类"桶
        std::function<const std::string*(const std::string&)> symbolSector;
        /// 行业码 → 行业名；未提供或返回空串 → UI 显示行业码
        std::function<const std::string*(const std::string&)> sectorName;
        /// 行情视图（close 矩阵重构行业毛收益）；空 → Brinson 不可算
        const factor::compute::IMarketDataView* view{nullptr};
        /// 基准逐日成分权重（date → symbol → w_b），key 集合即基准覆盖日
        /// 月频采样，同月各日共享同一份权重表（shared_ptr 零拷贝）
        std::map<domain::DomainDate,
                 std::shared_ptr<const std::unordered_map<std::string, double>>> benchmarkStockWeights;
        /// 回测总日序列（totalDays/缺口统计用）
        std::vector<domain::DomainDate> backtestDays;
    };

    /// @brief 计算策略归因报告（行业/个股/Brinson 三块）
    [[nodiscard]] StrategyAttributionReport compute(const Inputs& inputs) const;

    /// @brief 恒等式裁决阈值：|AE+SE+IE − Σ_{aligned}(R_p−R_b)|
    static constexpr double kIdentityTolerance = 1e-6;

    /// @brief 行业映射缺失时的兜底桶
    static constexpr const char* kUnclassifiedCode = "UNCLASSIFIED";
    static constexpr const char* kUnclassifiedName = "未分类";

private:
    /// @brief 对齐日中间上下文（逐日 Brinson 计算用）
    struct DayContext final {
        domain::DomainDate date;
        int rowToday{-1};  // view close 行（t）
        int rowNext{-1};   // view close 行（t+1）
        std::map<std::string, double> rawPortfolioWeight;       // 行业 ← Σ mktval/equity（展示口径）
        std::map<std::string, double> portfolioWeight;          // 行业 ← Σ w_p(i)（有效持仓，Brinson 口径）
        std::map<std::string, double> portfolioWeightedReturn;  // 行业 ← Σ w_p(i)·r_i
        std::map<std::string, double> benchmarkWeight;          // 行业 ← Σ w_b(i)（缺价剔除后）
        std::map<std::string, double> benchmarkWeightedReturn;  // 行业 ← Σ w_b(i)·r_i
        double portfolioTotalWeight{0.0};
        double benchmarkTotalWeight{0.0};
    };

    /// @brief 行业交易聚合中间结构（tradeLog 按行业归并）
    struct SectorAgg final {
        double totalPnl{0.0};
        double buyAmount{0.0};
        int tradeCount{0};
        std::set<std::string> symbols;
    };

    /// @brief 单日行业收益贡献（w_p·r_p − w_b·r_b 及效应项）
    struct DayBrinson final {
        double ae{0.0};
        double se{0.0};
        double ie{0.0};
        double excess{0.0};
        double portfolioReturn{0.0};
        double benchmarkReturn{0.0};
    };

    [[nodiscard]] std::string resolveSectorCode(const Inputs& inputs, const std::string& symbol) const;
    [[nodiscard]] std::string resolveSectorName(const Inputs& inputs, const std::string& code) const;
    [[nodiscard]] std::vector<DayContext> buildAlignedDays(const Inputs& inputs) const;
    [[nodiscard]] DayBrinson computeDayBrinson(const DayContext& day) const;
    [[nodiscard]] int computeMaxBenchmarkGapDays(const Inputs& inputs) const;

    /// @brief 判断行业码是否为"未分类"兜底桶
    [[nodiscard]] static bool isUnclassifiedCode(const std::string& code);

    void computeStocks(const std::vector<domain::strategy::BacktestTradeRecord>& tradeLog,
                       StrategyAttributionReport& report) const;
    void computeSectors(const Inputs& inputs, const std::vector<DayContext>& days,
                        StrategyAttributionReport& report) const;
    void computeBrinson(const std::vector<DayContext>& days, BrinsonBreakdown& out) const;

    /// @brief 行业权重表取值（缺失行业 = 0）
    [[nodiscard]] static double valueOrZero(const std::map<std::string, double>& table,
                                            const std::string& code);
};

} // namespace domain::attribution
