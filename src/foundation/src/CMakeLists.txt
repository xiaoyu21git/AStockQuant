file(GLOB FOUNDATION_SOURCES_CXX "*.cxx")
target_sources(foundation PRIVATE
    # config/ConfigParser.cpp
    # config/JsonParser.cpp
    # config/YamlParser.cpp
    Fs/Fs.cpp
    json/json_facade.cpp
    log/logger.cpp
    net/Net_facade.cpp
    thread/ThreadPoolExecutor.cpp
    Utils/time.cpp
    Utils/random.cpp
    Utils/system.cpp
    Yaml/Yaml_facade.cpp
    foundation.cpp
    ${FOUNDATION_SOURCES_CXX}
)