file(GLOB FOUNDATION_NET_SOURCES_CXX "*.cxx")
target_sources(foundation_net PRIVATE
    #NetManager.cpp
    Net_facade.cpp
    ${FOUNDATION_NET_SOURCES_CXX}
)