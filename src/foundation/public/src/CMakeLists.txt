file(GLOB FOUNDATION_PUBLIC_SOURCES_CXX "*.cxx")
target_sources(foundation_public PRIVATE
    PublicManager.cpp
    ${FOUNDATION_PUBLIC_SOURCES_CXX}
)   