add_library(domain_model STATIC)
add_subdirectory(src)
#find_package(CURL CONFIG REQUIRED)
# 如果用到了第三方
target_link_libraries(domain_model
    PRIVATE
        foundation  
)
target_include_directories(domain_model
    PUBLIC
        include
    PRIVATE
        src
)