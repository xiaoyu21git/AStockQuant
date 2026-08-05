#pragma once
// ICandidatePoolSelector — 因子候选池选择器
//
// 职责: 因子扫全市场 → compositeScore 排名 → 输出候选标的池
//       回答"哪些标的值得关注"，不参与买卖决策。
//
// 策略在候选池内独立判断买卖时机，因子和策略不再"加权投票"。
//
// 多态子类消除 combineMode 的运行时 if/else 分岔:
//   RankOnlyPoolSelector       — 不做硬过滤，直接 compositeScore 排名
//   IntersectionPoolSelector   — 所有因子 passFilter 才进入排名
//   UnionPoolSelector          — 任一因子 passAnyFilter 即可进入排名
//   NullPoolSelector           — 因子关闭时返回空池(策略扫全市场)

#include "FactorSignalProcessor.h"

#include <memory>
#include <string>
#include <vector>

namespace domain::strategy {

class ICandidatePoolSelector {
public:
    virtual ~ICandidatePoolSelector() = default;

    /// @brief 扫全市场 → 因子排名 → 输出候选标的池 (按 compositeScore 降序)
    /// @param processor 因子信号处理器（过滤/排名/综合得分）
    /// @return 候选标的列表 (fullSymbol)，按因子综合分降序；
    ///         空列表 = 因子关闭，策略应扫全市场
    [[nodiscard]] virtual std::vector<std::string> selectPool(
        const FactorSignalProcessor& processor) const = 0;
};

// ── 纯排名：不做硬过滤，直接 compositeScore 排名 ──
class RankOnlyPoolSelector final : public ICandidatePoolSelector {
public:
    [[nodiscard]] std::vector<std::string> selectPool(
        const FactorSignalProcessor& processor) const override;
};

// ── 交集：所有因子 passFilter 才进入排名 ──
class IntersectionPoolSelector final : public ICandidatePoolSelector {
public:
    [[nodiscard]] std::vector<std::string> selectPool(
        const FactorSignalProcessor& processor) const override;
};

// ── 并集：任一因子 passAnyFilter 即可进入排名 ──
class UnionPoolSelector final : public ICandidatePoolSelector {
public:
    [[nodiscard]] std::vector<std::string> selectPool(
        const FactorSignalProcessor& processor) const override;
};

// ── 配额制：各因子独立排名, 按权重比例分池, 合并去重 ──
class QuotaPoolSelector final : public ICandidatePoolSelector {
public:
    [[nodiscard]] std::vector<std::string> selectPool(
        const FactorSignalProcessor& processor) const override;
};

// ── 因子关闭：返回空池 → 策略扫全市场 ──
class NullPoolSelector final : public ICandidatePoolSelector {
public:
    [[nodiscard]] std::vector<std::string> selectPool(
        const FactorSignalProcessor& /*processor*/) const override {
        return {};
    }
};

/// @brief 根据融合模式创建对应的 PoolSelector 实例（编译期多态，无运行时 if/else）
[[nodiscard]] std::unique_ptr<ICandidatePoolSelector> createPoolSelector(FactorCombineMode mode);

} // namespace domain::strategy
