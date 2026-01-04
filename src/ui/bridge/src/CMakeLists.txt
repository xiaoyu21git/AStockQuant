file(GLOB BRIDGE_SOURCES_CXX "*.cxx")
target_sources(bridge PRIVATE
    TradeRecordModel.cpp
    registerQmlTypes.cpp
    ${BRIDGE_SOURCES_CXX}
)