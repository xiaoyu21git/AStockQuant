add_library(domain_indicators STATIC)
add_subdirectory(src)
#find_package(CURL CONFIG REQUIRED)
# 如果用到了第三方
target_link_libraries(domain_indicators
    PUBLIC
        domain_model
)
target_include_directories(domain_indicators
    PUBLIC
        include
    PRIVATE
        src
)