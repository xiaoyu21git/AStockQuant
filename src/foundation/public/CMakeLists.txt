add_library(foundation_public STATIC)
add_subdirectory(src)
# target_sources(foundation_public
#     PRIVATE
#         src/YamlManager.cpp
# )

# 如果用到了第三方
target_link_libraries(foundation_public
    PRIVATE
        foundation_json
        foundation_net
        foundation_yaml
)
target_include_directories(foundation_public
    PUBLIC
        include
    PRIVATE
        src
)