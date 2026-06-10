#pragma once

#include "../../factor/include/factor_compute/FactorSignalTypes.h"
#include "../../factor/include/factor_compute/IMarketDataView.h"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace domain::strategy {

//
// 公共类型
//
struct RiskEvaluationResult final           { bool tradable{true}; std::string blockReason; };
struct PositionInfo final                   { double qty{0.0}; double cost{0.0}; double unreal{0.0}; double real{0.0}; };
struct AccountInfo final                    { double cash{0.0}; double equity{0.0}; double margin{0.0}; double leverage{0.0}; };
struct PositionLimitResult final            { bool exceeded{false}; std::string reason; double ratio{0.0}; double maxRatio{0.0}; };

// ── 账户管理器（纯 C++，快照返回） ──
class AccountManager {
public:
    AccountManager(double initCash);
    [[nodiscard]] AccountInfo snapshot() const;
    void processTrade(double value);
    void reset(double initCash);
private:
    double cash_{0.0}, equity_{0.0};
};

// ── 风控规则接口 ──
class IRiskRule {
public:
    virtual ~IRiskRule() = default;
    [[nodiscard]] virtual RiskEvaluationResult evaluate(
        int t, int n,
        const factor::compute::SignalSet& sig,
        const factor::compute::IMarketDataView& view) const = 0;
};

// ── 事前 ──
class LimitUpDownRule final : public IRiskRule {
    double lu_, ld_;
    int tc_, ic_;
public:
    LimitUpDownRule(double, double, int, int);
    [[nodiscard]] RiskEvaluationResult evaluate(int, int, const factor::compute::SignalSet&, const factor::compute::IMarketDataView&) const override;
};
class LiquidityRule final : public IRiskRule {
    double mv_;
    int tc_, ic_;
public:
    LiquidityRule(double, int, int);
    [[nodiscard]] RiskEvaluationResult evaluate(int, int, const factor::compute::SignalSet&, const factor::compute::IMarketDataView&) const override;
};
class SuspensionRule final : public IRiskRule {
    std::vector<uint8_t> mask_;
    int tc_, ic_;
public:
    SuspensionRule(std::vector<uint8_t>, int, int);
    [[nodiscard]] RiskEvaluationResult evaluate(int, int, const factor::compute::SignalSet&, const factor::compute::IMarketDataView&) const override;
};
class SecurityListRule final : public IRiskRule {
    std::vector<int> blocked_;
public:
    explicit SecurityListRule(std::vector<int>);
    [[nodiscard]] RiskEvaluationResult evaluate(int, int, const factor::compute::SignalSet&, const factor::compute::IMarketDataView&) const override;
};

// ── 仓位 & 资金 ──
class PositionLimitRule final : public IRiskRule {
    double maxQty_;
    int tc_, ic_;
public:
    PositionLimitRule(double maxQuantity, int, int);
    [[nodiscard]] RiskEvaluationResult evaluate(int, int, const factor::compute::SignalSet&, const factor::compute::IMarketDataView&) const override;
};
class MaxDrawdownRule final : public IRiskRule {
    double maxDD_, peak_;
    int tc_, ic_;
public:
    MaxDrawdownRule(double, double, int, int);
    [[nodiscard]] RiskEvaluationResult evaluate(int, int, const factor::compute::SignalSet&, const factor::compute::IMarketDataView&) const override;
};
class LeverageLimitRule final : public IRiskRule {
    double maxLev_, eq_;
    int tc_, ic_;
public:
    LeverageLimitRule(double, double, int, int);
    [[nodiscard]] RiskEvaluationResult evaluate(int, int, const factor::compute::SignalSet&, const factor::compute::IMarketDataView&) const override;
};

// ── 管道 ──
class RiskPipeline final {
    std::vector<std::unique_ptr<IRiskRule>> rules_;
public:
    void addRule(std::unique_ptr<IRiskRule> r);
    void clear();
    [[nodiscard]] std::vector<uint8_t> evaluate(
        const factor::compute::SignalSet& signal,
        const factor::compute::IMarketDataView& view) const;
};

} // namespace domain::strategy