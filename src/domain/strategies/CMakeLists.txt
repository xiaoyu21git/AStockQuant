add_library(domain_strategies STATIC)
add_subdirectory(src)
#find_package(CURL CONFIG REQUIRED)
# 如果用到了第三方
target_link_libraries(domain_strategies
    PUBLIC
        domain_model
        domain_signals
)
target_include_directories(domain_strategies
    PUBLIC
        include
    PRIVATE
        src
)