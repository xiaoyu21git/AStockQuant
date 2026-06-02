#include "factor_compute/IFactorComputeOperatorExtension.h"

#include "factor_compute/ConstComputeOpRegistrar.h"
#include "factor_compute/DefaultComputeOpRegistrar.h"

#include "factor_compute/FactorComputeToken.h"

#include <cmath>
#include <memory>
#include <vector>

namespace factor::compute {

namespace {

class CloseOperator final : public IComputeOp {
public:
    void execute(
        const IFactorOperatorLibrary&,
        NumericConstMatrixView closeView,
        NumericMatrixView outputView) const override
    {
        for (int32_t row = 0; row < closeView.rowCount; ++row) {
            for (int32_t column = 0; column < closeView.columnCount; ++column) {
                const size_t inputFlat = static_cast<size_t>(row) * static_cast<size_t>(closeView.rowStride)
                    + static_cast<size_t>(column);
                const size_t outputFlat = static_cast<size_t>(row) * static_cast<size_t>(outputView.rowStride)
                    + static_cast<size_t>(column);
                outputView.data[outputFlat] = closeView.data[inputFlat];
            }
        }
    }
};

class Lag1Operator final : public IComputeOp {
public:
    void execute(
        const IFactorOperatorLibrary& factorOperatorLibrary,
        NumericConstMatrixView closeView,
        NumericMatrixView outputView) const override
    {
        factorOperatorLibrary.lag(closeView, kLagWindow, outputView);
    }
};

class RollingMean2Operator final : public IComputeOp {
public:
    void execute(
        const IFactorOperatorLibrary& factorOperatorLibrary,
        NumericConstMatrixView closeView,
        NumericMatrixView outputView) const override
    {
        factorOperatorLibrary.rollingMean(closeView, kRollingWindow, outputView);
    }
};

class RollingSum2Operator final : public IComputeOp {
public:
    void execute(
        const IFactorOperatorLibrary& factorOperatorLibrary,
        NumericConstMatrixView closeView,
        NumericMatrixView outputView) const override
    {
        factorOperatorLibrary.rollingSum(closeView, kRollingWindow, outputView);
    }
};

class RankOperator final : public IComputeOp {
public:
    void execute(
        const IFactorOperatorLibrary& factorOperatorLibrary,
        NumericConstMatrixView closeView,
        NumericMatrixView outputView) const override
    {
        factorOperatorLibrary.rank(closeView, outputView);
    }
};

class GroupByMeanOperator final : public IComputeOp {
public:
    void execute(
        const IFactorOperatorLibrary& factorOperatorLibrary,
        NumericConstMatrixView closeView,
        NumericMatrixView outputView) const override
    {
        std::vector<uint32_t> groupKeys(static_cast<size_t>(closeView.columnCount), kDefaultGroupKey);
        GroupKeyView groupKeyView;
        groupKeyView.data = groupKeys.data();
        groupKeyView.count = closeView.columnCount;
        factorOperatorLibrary.groupByMean(closeView, groupKeyView, outputView);
    }
};

class ConstantValueComputeOperator final : public IComputeOp {
public:
    explicit ConstantValueComputeOperator(double value) noexcept
        : value_(value)
    {
    }

    void execute(
        const IFactorOperatorLibrary&,
        NumericConstMatrixView input,
        NumericMatrixView output) const override
    {
        for (int32_t row = 0; row < input.rowCount; ++row) {
            for (int32_t column = 0; column < input.columnCount; ++column) {
                const size_t flat = static_cast<size_t>(row) * static_cast<size_t>(output.rowStride)
                    + static_cast<size_t>(column);
                output.data[flat] = value_;
            }
        }
    }

private:
    double value_{0.0};
};

class ConstantValueComputeOperatorRegistrar final : public IComputeOpRegistrar {
public:
    explicit ConstantValueComputeOperatorRegistrar(const ConstOpConfig& config) noexcept
        : config_(config)
    {
    }

    bool registerOperators(IFactorComputeOperatorRegistry& registry) const override
    {
        return registry.registerOperator(
            config_.computeToken(),
            std::make_unique<ConstantValueComputeOperator>(config_.outputValue()));
    }

private:
    const ConstOpConfig config_;
};

} // namespace

bool DefaultOpRegistrar::registerOperators(
    IFactorComputeOperatorRegistry& registry) const
{
    bool ok = true;
    ok = ok && registry.registerOperator(toToken(ComputeToken::Close), std::make_unique<CloseOperator>());
    ok = ok && registry.registerOperator(toToken(ComputeToken::Lag1), std::make_unique<Lag1Operator>());
    ok = ok && registry.registerOperator(toToken(ComputeToken::RollingMean2), std::make_unique<RollingMean2Operator>());
    ok = ok && registry.registerOperator(toToken(ComputeToken::RollingSum2), std::make_unique<RollingSum2Operator>());
    ok = ok && registry.registerOperator(toToken(ComputeToken::Rank), std::make_unique<RankOperator>());
    ok = ok && registry.registerOperator(toToken(ComputeToken::GroupByMean), std::make_unique<GroupByMeanOperator>());
    return ok;
}

ConstOpConfig::ConstOpConfig(uint32_t computeToken, double outputValue) noexcept
    : computeToken_(computeToken)
    , outputValue_(outputValue)
{
}

uint32_t ConstOpConfig::computeToken() const noexcept
{
    return computeToken_;
}

double ConstOpConfig::outputValue() const noexcept
{
    return outputValue_;
}

bool ConstOpConfig::isValid() const noexcept
{
    return computeToken_ != 0U && std::isfinite(outputValue_);
}

std::unique_ptr<IComputeOpRegistrar>
ConstOpRegistrarFactory::create(const ConstOpConfig& config) const
{
    if (!config.isValid()) {
        return nullptr;
    }

    return std::make_unique<ConstantValueComputeOperatorRegistrar>(config);
}

} // namespace factor::compute

