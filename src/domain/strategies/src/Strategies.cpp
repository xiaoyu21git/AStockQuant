#include "Strategies.h"

namespace domain::strategies {

EarningsSurpriseStrategy::EarningsSurpriseStrategy(
	const StrategyCommonConfig& commonConfig,
	const StrategyMetadata& metadata,
	const EarningsSurpriseStrategySpec& spec)
	: IStrategy(commonConfig, metadata)
	, spec_(spec)
{
}

StrategyType EarningsSurpriseStrategy::strategyType() const
{
	return StrategyType::EARNINGS_SURPRISE;
}

const EarningsSurpriseStrategySpec& EarningsSurpriseStrategy::spec() const noexcept
{
	return spec_;
}

MachineLearningSelectionStrategy::MachineLearningSelectionStrategy(
	const StrategyCommonConfig& commonConfig,
	const StrategyMetadata& metadata,
	const MachineLearningSelectionStrategySpec& spec)
	: IStrategy(commonConfig, metadata)
	, spec_(spec)
{
}

StrategyType MachineLearningSelectionStrategy::strategyType() const
{
	return StrategyType::MACHINE_LEARNING_SELECTION;
}

const MachineLearningSelectionStrategySpec& MachineLearningSelectionStrategy::spec() const noexcept
{
	return spec_;
}

MultiFactorSelectionStrategy::MultiFactorSelectionStrategy(
	const StrategyCommonConfig& commonConfig,
	const StrategyMetadata& metadata,
	const MultiFactorSelectionStrategySpec& spec)
	: IStrategy(commonConfig, metadata)
	, spec_(spec)
{
}

StrategyType MultiFactorSelectionStrategy::strategyType() const
{
	return StrategyType::MULTI_FACTOR_SELECTION;
}

const MultiFactorSelectionStrategySpec& MultiFactorSelectionStrategy::spec() const noexcept
{
	return spec_;
}

OrderFlowImbalanceStrategy::OrderFlowImbalanceStrategy(
	const StrategyCommonConfig& commonConfig,
	const StrategyMetadata& metadata,
	const OrderFlowImbalanceStrategySpec& spec)
	: IStrategy(commonConfig, metadata)
	, spec_(spec)
{
}

StrategyType OrderFlowImbalanceStrategy::strategyType() const
{
	return StrategyType::ORDER_FLOW_IMBALANCE;
}

const OrderFlowImbalanceStrategySpec& OrderFlowImbalanceStrategy::spec() const noexcept
{
	return spec_;
	}

StatisticalPairTradingStrategy::StatisticalPairTradingStrategy(
	const StrategyCommonConfig& commonConfig,
	const StrategyMetadata& metadata,
	const StatisticalPairTradingStrategySpec& spec)
	: IStrategy(commonConfig, metadata)
	, spec_(spec)
{
}

StrategyType StatisticalPairTradingStrategy::strategyType() const
{
	return StrategyType::STATISTICAL_PAIR_TRADING;
}

const StatisticalPairTradingStrategySpec& StatisticalPairTradingStrategy::spec() const noexcept
{
	return spec_;
}

VolatilitySpreadStrategy::VolatilitySpreadStrategy(
	const StrategyCommonConfig& commonConfig,
	const StrategyMetadata& metadata,
	const VolatilitySpreadStrategySpec& spec)
	: IStrategy(commonConfig, metadata)
	, spec_(spec)
{
}

StrategyType VolatilitySpreadStrategy::strategyType() const
{
	return StrategyType::VOLATILITY_SPREAD;
}

const VolatilitySpreadStrategySpec& VolatilitySpreadStrategy::spec() const noexcept
{
	return spec_;
}

} // namespace domain::strategies
