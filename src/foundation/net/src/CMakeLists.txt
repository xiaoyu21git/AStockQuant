file(GLOB FOUNDATION_NET_SOURCES_CXX "*.cxx")
target_sources(foundation_net PRIVATE
    NetManager.cpp
    HttpClient.cpp
    ${FOUNDATION_NET_SOURCES_CXX}
)