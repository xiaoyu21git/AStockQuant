#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace domain::attribution {

/// @brief 行业归因条目（行业维度已实现盈亏 + 组合/基准权重对比）
struct SectorAttributionItem final {
    std::string sectorCode;          // industry_code（行业名表空时 UI 兜底显示码）
    std::string sectorName;          // 行业名（缺失时为空串，UI 显示行业码）
    double totalRealizedPnl{0.0};    // 行业已实现盈亏合计（元）
    double buyAmount{0.0};           // 行业买入成本合计（元，收益率分母）
    double realizedReturn{0.0};      // 已实现收益率 = totalRealizedPnl / buyAmount
    double averageWeight{0.0};       // 组合平均权重（对齐日 mktval/equity 平均）
    double benchmarkWeight{0.0};     // 基准平均权重（对齐日平均，展示超配/低配）
    double returnContribution{0.0};  // 有符号贡献占比 = pnl_s / Σ|pnl_s|
    int tradeCount{0};               // 成交笔数
    int stockCount{0};               // 去重标的数
};

/// @brief 个股归因条目（tradeLog 纯聚合，无需 DB）
struct StockAttributionItem final {
    std::string symbol;         // fullSymbol（如 "300097.SZ"）
    double realizedPnl{0.0};    // 已实现盈亏合计（元，卖出记录累加）
    int buyCount{0};
    int sellCount{0};
    int winCount{0};            // 盈利卖出笔数（realizedPnl > 0）
};

/// @brief Brinson 择时/选股分解（仅对齐日期集合上累加）
struct BrinsonBreakdown final {
    double allocationEffect{0.0};    // 配置效应 AE = Σ(w_p−w_b)·r_b
    double selectionEffect{0.0};     // 选股效应 SE = Σ w_b·(r_p−r_b)
    double interactionEffect{0.0};   // 交互效应 IE = Σ(w_p−w_b)·(r_p−r_b)
    double excessReturn{0.0};        // 锚点 = Σ_{aligned}(R_p−R_b)
    double portfolioReturn{0.0};     // Σ_{aligned} R_p（重构毛收益）
    double benchmarkReturn{0.0};     // Σ_{aligned} R_b
    double identityError{0.0};       // |AE+SE+IE − excessReturn|（对齐口径）
    int alignedDays{0};              // 对齐交易日数
    int totalDays{0};                // 回测总日数
    int maxBenchmarkGapDays{0};      // 基准数据最大无覆盖间隔（天）
    bool lowDataQuality{false};      // maxBenchmarkGapDays > 回测期20% → true
    std::vector<double> cumulativeAe;  // 逐日累计（对齐日序列，曲线用）
    std::vector<double> cumulativeSe;
    std::vector<double> cumulativeIe;

    /// @brief 年度分解（跨年回测时按年分组累加）
    struct YearBreak final {
        int year{0};
        double ae{0.0};
        double se{0.0};
        double ie{0.0};
        double excess{0.0};
    };
    std::vector<YearBreak> yearly;
};

/// @brief 策略归因报告（行业 + 个股 + Brinson 三块）
struct StrategyAttributionReport final {
    std::vector<SectorAttributionItem> sectors;  // |贡献| 降序
    std::vector<StockAttributionItem> stocks;    // realizedPnl 降序
    BrinsonBreakdown brinson;
    bool hasSectorNames{false};  // 行业名表是否有数据（false → UI 显示代码 + notice）
    bool isValid{false};
    std::string notice;          // 口径/降级说明
};

} // namespace domain::attribution
