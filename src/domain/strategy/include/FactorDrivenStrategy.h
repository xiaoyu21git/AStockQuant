#pragma once
// FactorDrivenStrategy — 纯因子驱动策略
// 不使用技术指标, 直接用 AI 因子 + 传统因子综合分决定买卖
// 规则闸门保持强势兜底, 策略和规则各司其职

#include "IStrategyService.h"
#include "StrategyServiceTypes.h"
#include "../../strategies/include/StrategyDefinitionTypes.h"
#include "MarketDataService.h"
#include "FactorSignalProcessor.h"

#include <string>
#include <vector>

namespace domain::strategy {

class FactorDrivenStrategy : public IRuntimeStrategy {
public:
    struct Config {
        int    topN = 50;              // 选股数
        double entryScoreThreshold = 0.6; // 综合得分 > threshold 才买入
        double exitScoreThreshold  = 0.3; // 综合得分 < threshold 则卖出
        double maxWeightPerStock   = 0.05; // 单票上限 5%
        double minWeightPerStock   = 0.01;
        WeightScheme weightScheme   = WeightScheme::FactorScore;
    };

    FactorDrivenStrategy(StrategyInstanceId instanceId, const Config& cfg,
                         std::shared_ptr<const FactorSignalProcessor> processor);

    [[nodiscard]] StrategyInstanceId instanceId() const noexcept override { return m_instanceId; }
    [[nodiscard]] bool isEnabled() const noexcept override { return true; }
    [[nodiscard]] rules::RuleSetId ruleSetId() const noexcept override { return rules::kRuleSetAllPass; }
    [[nodiscard]] bool usesFactors() const noexcept override { return true; }

    void evaluate(const std::vector<RuntimeFactorSnapshot>& factorSnapshots,
                  const RuntimeStrategyContext& ctx,
                  std::vector<StrategySignal>& out) override;

    static std::shared_ptr<IRuntimeStrategy> create(
        StrategyInstanceId instanceId, const StrategyCreationParams& params);

private:
    StrategyInstanceId m_instanceId;
    Config m_cfg;
    std::shared_ptr<const FactorSignalProcessor> m_processor;
};

} // namespace domain::strategy
