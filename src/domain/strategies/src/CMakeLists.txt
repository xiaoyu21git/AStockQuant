file(GLOB DOMAIN_STRATEGIES_SOURCES_CXX "*.cxx")
target_sources(domain_strategies PRIVATE
    Strategies.cpp
    FixedBarStrategy.cpp
    MovingAverageStrategy.cpp
    ${DOMAIN_STRATEGIES_SOURCES_CXX}
)