file(GLOB ASTOCKQUANTAPP_SOURCES_CXX "*.cxx")
target_sources(astockquantapp PRIVATE
    registerQmlTypes.cpp
    ${ASTOCKQUANTAPP_SOURCES_CXX}
)