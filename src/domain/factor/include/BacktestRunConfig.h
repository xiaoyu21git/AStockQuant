#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// BacktestRunConfig — 回测运行时参数 (纯 C++ 结构体, 不含 QVariant/Qt 依赖)
// 由 Bridge 从 QML 的 QVariantMap 转换, 传给 Orchestrator
// ══════════════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <string>
#include <vector>
#include "../../types/DomainDate.h"
#include "CompositeFactorConfig.h"

namespace Factor::backtest {

/// @brief 数据源模式
enum class FactorMode : int {
    Single = 0,   // 单因子
    Dual   = 1,   // 双因子
    Composite = 2, // 组合因子
};

enum class DataSourceMode : int {
    Cache = 0,
    Live  = 1,
};

/// @brief 回测运行时参数 — Bridge 从 QML 转换后传给 Orchestrator
struct BacktestRunConfig {
    // ── 缓存配置 (从 QML 缓存页面选择) ──
    DataSourceMode dataSourceMode = DataSourceMode::Cache;
    int selectedDatasetId = 0;
    domain::DomainDate cacheStartDate;
    domain::DomainDate cacheEndDate;

    // ── 因子配置 ──
    FactorMode factorMode = FactorMode::Single;
    std::vector<std::string> factorIds;  // 只传 ID, 底层用 FactorInstanceManager 查数据库取配置

    // ── 组合因子配置 (仅 FactorMode::Composite 时有效) ──
    std::string compositeName;          // 组合因子名称（从 QML compositeDraftName 传入）
    std::vector<factor::CompositeChildSpec> compositeChildren;
    int compositeCombineMode = 0;      // CompositeCombineMode 枚举值
    int compositeMissingPolicy = 2;    // CompositeMissingPolicy 枚举值
    double compositeMinCoverageRatio = 0.5;

    // ── 回测参数 (从 QML 参数对话框传入) ──
    int numGroups = 5;
    int forwardDays = 30;
    int rebalanceDays = 15;
    double commissionRate = 0.001;
    double slippageRate = 0.001;
    double riskFreeRate = 0.02;
    double initialCapital = 1000000.0;
    std::string benchmarkSymbol = "000300.SH";
    std::string adjustPriceType = "pre";   // "pre" / "post"
    double winsorizeQuantile{0.005};       // 因子值双侧缩尾分位数（0.0=不缩尾），传递给 Orchestrator
    int marketEnvironmentProfile = 0;
    bool ascending{true};                   // 因子方向: true=值越大越好, false=值越小越好
    bool longOnly{false};                   // 禁止做空开关: true=策略收益仅多头腿(分组展示不变); false=多空(现有行为)
    double maxFwdRetAbsLimit{0.5};          // |前向收益|上限截断: 剔除 |ret|≥该值的样本, 默认0.5=现有行为, 放宽可减小对真反弹的误杀
    int workerThreads = 1;                  // 块级并行 worker 数 (<2 或块数<2 走串行路径, 同一套代码)
};

} // namespace Factor::backtest
