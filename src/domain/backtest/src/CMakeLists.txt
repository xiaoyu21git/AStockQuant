file(GLOB DOMAIN_BACKTEST_SOURCES_CXX "*.cxx")

target_sources(domain_backtest PRIVATE
    BacktestEngine.cpp
    ${DOMAIN_BACKTEST_SOURCES_CXX}
)
