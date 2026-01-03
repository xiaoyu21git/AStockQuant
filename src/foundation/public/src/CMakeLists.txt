file(GLOB FOUNDATION_PUBLIC_SOURCES_CXX "*.cxx")
target_sources(foundation_public PRIVATE
    foundation_public.cpp
    foundation.cpp
    ${FOUNDATION_PUBLIC_SOURCES_CXX}
)   