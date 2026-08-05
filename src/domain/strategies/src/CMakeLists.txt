target_sources(domain_strategies PRIVATE
    Strategies.cpp
    IStrategy.cxx
    BollingerBandMeanReversionStrategy.cxx
    DoubleMovingAverageStrategy.cxx
    EarningsSurpriseStrategy.cxx
    MachineLearningSelectionStrategy.cxx
    OrderFlowImbalanceStrategy.cxx
    RiskParityAllocationStrategy.cxx
    RsiMeanReversionStrategy.cxx
    StatisticalPairTradingStrategy.cxx
    TurtleBreakoutStrategy.cxx
    VolatilitySpreadStrategy.cxx
)