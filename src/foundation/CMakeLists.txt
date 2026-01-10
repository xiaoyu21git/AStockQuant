
add_library(foundation STATIC)
set(LIBS_ROOT "C:/libs")
find_package(CURL CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
add_subdirectory(src)
# target_sources(foundation
#     PRIVATE
#         src/YamlManager.cpp
# )
# target_include_directories(foundation
#     PUBLIC
#         ${CMAKE_CURRENT_SOURCE_DIR}/include
#         "${LIBS_ROOT}/nlohmann_json/include"
# )

# 如果用到了第三方
target_link_libraries(foundation
    PRIVATE
        CURL::libcurl
        yaml-cpp
)
target_include_directories(foundation
    PUBLIC
        include
        "${LIBS_ROOT}/nlohmann_json/include"
    PRIVATE
        src
)

#add_subdirectory(test)