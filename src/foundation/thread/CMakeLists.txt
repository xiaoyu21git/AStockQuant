add_library(foundation_thread STATIC)
add_subdirectory(src)
# target_sources(foundation_public
#     PRIVATE
#         src/YamlManager.cpp
# )

#find_package(yaml-cpp CONFIG REQUIRED)
# 如果用到了第三方
# target_link_libraries(foundation_public
#     PUBLIC
#         yaml-cpp
# )
target_include_directories(foundation_thread
    PUBLIC
        include
    PRIVATE
        src
)