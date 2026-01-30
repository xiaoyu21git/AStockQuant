file(GLOB FOUNDATION_SOURCES_CXX "*.cxx")
target_sources(foundation PRIVATE
    config/ConfigNode.cpp
    config/ConfigLoader.cpp
    config/ConfigManager.cpp
    config/JsonConfigProvider.cpp
    config/YamlConfigProvider.cpp
    core/exception.cpp
    Fs/Fs.cpp
    Fs/file.cpp
    Fs/FileWatcher.cpp
    json/json_facade.cpp
    log/logger.cpp
    # 网络相关实现。Net_facade.cpp 使用 libcurl，当 BUILD_WITH_CURL=OFF 时会用 Net_stub.cpp 替代。
)

if(BUILD_WITH_CURL)
    target_sources(foundation PRIVATE
        net/Net_facade.cpp
        net/http_request_builder.cpp
    )
else()
    target_sources(foundation PRIVATE
        net/Net_stub.cpp
        net/http_request_builder.cpp
    )
    message(STATUS "BUILD_WITH_CURL=OFF: using Net_stub.cpp instead of Net_facade.cpp")
endif()
    thread/ThreadPoolExecutor.cpp
    thread/InlineExecutor.cpp
    thread/thread_pool.cpp
    thread/ThreadExit.cpp
    Utils/CacheUtils.cpp
    Utils/time.cpp
    Utils/random.cpp
    Utils/system.cpp
    Utils/String.cpp
    Utils/Uuid.cpp
    Yaml/Yaml_facade.cpp

    foundation.cpp
    ${FOUNDATION_SOURCES_CXX}
)