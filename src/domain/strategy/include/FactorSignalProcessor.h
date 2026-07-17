#pragma once
// FactorSignalProcessor — 混合模式因子信号处理(过滤+缩放)
// 在策略产出的信号上叠加因子辅助，不改策略引擎核心逻辑。
// isFactorType=true 时启用；factorIds 为空时直接跳过零开销。

#include <string>
#include <unordered_map>
#include <vector>

namespace domain::strategy {

struct FactorFilterConfig {
    std::string factorId;
    double minPercentile{0.1};   // 剔除因子值排名后 N% 的标的(默认后10%)
};

struct FactorScaleConfig {
    std::string factorId;
    double influence{1.0};       // 影响力系数(默认1.0=不调整)
};

/// 因子信号处理器：B(硬过滤) → A(软缩放)
class FactorSignalProcessor {
public:
    void setFilters(const std::vector<FactorFilterConfig>& filters) { m_filters = filters; }
    void setScalers(const std::vector<FactorScaleConfig>& scalers) { m_scalers = scalers; }
    bool enabled() const { return !m_filters.empty() || !m_scalers.empty(); }

    /// 配置涉及的全部因子 ID (filters ∪ scalers, 去重)
    [[nodiscard]] std::vector<std::string> factorIds() const;

    /// 更新单个因子的快照(日频每因子调用一次，日内复用)。
    /// 快照统计量(均值/标准差/过滤阈值)在此按值排序后预计算:
    /// 求和顺序确定 → 跨进程结果可复现; passFilter/scaleFactor 退化为 O(1) 查询
    void updateSnapshot(const std::string& factorId,
                        const std::unordered_map<std::string, double>& factorValues);

    /// 过滤：返回 true 表示该标的通过所有过滤条件
    [[nodiscard]] bool passFilter(const std::string& symbol) const;

    /// 缩放：返回乘数(默认1.0，无因子时为1.0)
    [[nodiscard]] double scaleFactor(const std::string& symbol) const;

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
    // factorId → 快照与统计量
    std::unordered_map<std::string, FactorSnapshotStats> m_snapshot;
};

} // namespace domain::strategy
