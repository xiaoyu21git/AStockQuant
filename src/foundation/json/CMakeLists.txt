add_library(foundation_json STATIC)
add_subdirectory(src)
find_package(nlohmann_json  REQUIRED)
# 如果用到了第三方
target_link_libraries(foundation_json
    PUBLIC
        nlohmann_json::nlohmann_json
)
target_include_directories(foundation_json
    PUBLIC
        include
    PRIVATE
        src
)