file(GLOB DOMAIN_INDICATORS_SOURCES_CXX "*.cxx")
target_sources(domain_indicators PRIVATE
    Indicator.cpp
    SMA.cpp
    SimpleMovingAverage.cpp
    batch_technical_indicators.cpp
    ${DOMAIN_INDICATORS_SOURCES_CXX}
)