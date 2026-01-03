file(GLOB FOUNDATION_JSON_SOURCES_CXX "*.cxx")
target_sources(foundation_json PRIVATE
    json_facade.cpp
    ${FOUNDATION_JSON_SOURCES_CXX}
)