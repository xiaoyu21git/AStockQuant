add_library(foundation_net STATIC)
add_subdirectory(src)
find_package(CURL CONFIG REQUIRED)
# 如果用到了第三方
target_link_libraries(foundation_net
    PUBLIC
        CURL::libcurl
)
target_include_directories(foundation_net
    PUBLIC
        include
    PRIVATE
        src
)