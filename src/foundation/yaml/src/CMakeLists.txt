file(GLOB FOUNDATION_YAML_SOURCES_CXX "*.cxx")
target_sources(foundation_yaml PRIVATE
    YamlManager.cpp
    YamlConfig.cpp
    ${FOUNDATION_YAML_SOURCES_CXX}
)