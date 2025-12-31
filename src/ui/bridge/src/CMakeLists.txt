file(GLOB BRIDGE_SOURCES_CXX "*.cxx")
target_sources(bridge PRIVATE
    registerQmlTypes.cpp
    ${BRIDGEI_SOURCES_CXX}
)