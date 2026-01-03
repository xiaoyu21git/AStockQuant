add_library(domain_signals STATIC)
add_subdirectory(src)
#find_package(CURL CONFIG REQUIRED)
# 如果用到了第三方
target_link_libraries(domain_signals
    PUBLIC
        domain_model
        domain_indicators
)
target_include_directories(domain_signals
    PUBLIC
        include
    PRIVATE
        src
)