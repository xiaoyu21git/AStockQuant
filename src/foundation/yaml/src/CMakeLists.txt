file(GLOB FOUNDATION_YAML_SOURCES_CXX "*.cxx")
target_sources(foundation_yaml PRIVATE
    Yaml_facade.cpp
    ${FOUNDATION_YAML_SOURCES_CXX}
)