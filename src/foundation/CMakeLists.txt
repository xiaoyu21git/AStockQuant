add_library(astock_foundation STATIC)
add_subdirectory(json)
add_subdirectory(net)
add_subdirectory(yaml)
add_subdirectory(public)
target_sources(astock_foundation
    PRIVATE
)


# 第三方库（示例）
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(CURL REQUIRED)

target_link_libraries(astock_foundation
    PRIVATE
        nlohmann_json::nlohmann_json
        yaml-cpp
        CURL::libcurl
)
target_include_directories(qapi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/json/include
        ${CMAKE_CURRENT_SOURCE_DIR}/net/include
        ${CMAKE_CURRENT_SOURCE_DIR}/yaml/include
        ${CMAKE_CURRENT_SOURCE_DIR}/public
    PRIVATE
        src
)