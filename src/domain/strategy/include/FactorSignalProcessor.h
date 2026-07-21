#pragma once
// FactorSignalProcessor — 混合模式因子选股: 过滤 → 加权综合排名 → 选 top N
// 多因子按配置权重汇总为单一排名分数, 所有策略类型均支持因子覆盖层
// combineMode 控制多因子结合方式: rank_only(纯排名) / intersection(交集) / union(并集)

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace domain::strategy {

/// 多因子结合模式: 控制多个因子如何共同决定标的入选
enum class FactorCombineMode : std::uint8_t {
    RankOnly = 0,      // 纯加权排名 — 不做硬过滤, 直接 compositeScore 排名选 top N
    Intersection = 1,  // 交集 — 所有因子 passFilter 才进入排名
    Union = 2,         // 并集 — 任一因子 passFilter 即可进入排名
};

struct FactorFilterConfig {
    std::string factorId;
    double minPercentile{0.1};   // 剔除因子值排名后 N% 的标的(默认后10%)
};

struct FactorScaleConfig {
    std::string factorId;
    double influence{1.0};       // 影响力系数(默认1.0=不调整)
};

/// 因子信号处理器：过滤 → 加权综合排名 → 选股
class FactorSignalProcessor {
public:
    void setFilters(const std::vector<FactorFilterConfig>& filters) { m_filters = filters; }
    void setScalers(const std::vector<FactorScaleConfig>& scalers) { m_scalers = scalers; }
    void setTargetPositionCount(int n) { m_targetPositionCount = n; }
    void setMinimumCompositeScore(double s) { m_minimumCompositeScore = s; }
    void setCombineMode(FactorCombineMode mode) { m_combineMode = mode; }
    void setStrategyBlendWeight(double w) { m_strategyBlendWeight = w; }
    void setFactorInfluence(const std::unordered_map<std::string, double>& inf) { m_factorInfluence = inf; }
    [[nodiscard]] int targetPositionCount() const { return m_targetPositionCount; }
    [[nodiscard]] double minimumCompositeScore() const { return m_minimumCompositeScore; }
    [[nodiscard]] FactorCombineMode combineMode() const { return m_combineMode; }
    [[nodiscard]] double strategyBlendWeight() const { return m_strategyBlendWeight; }
    bool enabled() const { return !m_filters.empty() || !m_scalers.empty(); }

    /// 配置涉及的全部因子 ID (filters ∪ scalers, 去重)
    [[nodiscard]] std::vector<std::string> factorIds() const;

    /// 更新单个因子的快照(日频每因子调用一次，日内复用)。
    /// 快照统计量(均值/标准差/过滤阈值)在此按值排序后预计算:
    /// 求和顺序确定 → 跨进程结果可复现; passFilter/scaleFactor 退化为 O(1) 查询
    void updateSnapshot(const std::string& factorId,
                        const std::unordered_map<std::string, double>& factorValues);

    /// 交集过滤: 所有因子全部通过才返回 true
    [[nodiscard]] bool passFilter(const std::string& symbol) const;

    /// 并集过滤: 任一因子通过即返回 true
    [[nodiscard]] bool passAnyFilter(const std::string& symbol) const;

    /// 加权综合得分: Σ(weight_i × normalized_factor_value_i)
    /// 多因子按配置权重汇总为单一排名分数，因子数据缺失返回 0.0
    [[nodiscard]] double compositeScore(const std::string& symbol) const;

    /// 缩放：返回乘数(默认1.0，无因子时为1.0)
    [[nodiscard]] double scaleFactor(const std::string& symbol) const;

    /// 快照中全部标的集合 — 因子独立选股的候选池
    [[nodiscard]] std::vector<std::string> allSymbols() const;

private:
    /// 单因子快照 + 预计算统计量
    struct FactorSnapshotStats {
        std::unordered_map<std::string, double> values;  // symbol → value
        double mean{0.0};
        double stdev{0.0};
        double filterThreshold{0.0};   // minPercentile 分位对应的值下界
        bool hasThreshold{false};
    };

    std::vector<FactorFilterConfig> m_filters;
    std::vector<FactorScaleConfig> m_scalers;
    // compositeScore 专用: factorId → 归一化权重(Σ=1.0), 与 scaleFactor 的 influence 分离
    std::unordered_map<std::string, double> m_factorInfluence;
    FactorCombineMode m_combineMode{FactorCombineMode::RankOnly};
    double m_strategyBlendWeight{0.5};  // 策略信号在最终分数中的权重(0.5=策略50%+因子50%)
    int m_targetPositionCount{10};
    double m_minimumCompositeScore{0.0};
    // factorId → 快照与统计量
    std::unordered_map<std::string, FactorSnapshotStats> m_snapshot;
};

} // namespace domain::strategy
