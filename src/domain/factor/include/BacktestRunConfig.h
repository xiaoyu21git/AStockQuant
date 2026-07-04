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
    int marketEnvironmentProfile = 0;

    // ── 预检预注入字段 (从预检结果传入，避免 Orchestrator 重复 createInstance) ──
    std::vector<std::string> preResolvedExtraFields;  // 已排除核心5字段的额外字段列表
    bool hasPreResolvedFields{false};                  // 为 true 时跳过 orchestrator 的 createInstance 步骤
};

} // namespace Factor::backtest
