file(GLOB DOMAIN_SIGNALS_SOURCES_CXX "*.cxx")
target_sources(domain_signals PRIVATE
    Signals.cpp
    CrossSignal.cpp
    ThresholdSignal.cpp
    ${DOMAIN_SIGNALS_SOURCES_CXX}
)