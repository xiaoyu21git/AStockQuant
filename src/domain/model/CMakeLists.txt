add_library(domain_model STATIC)
add_subdirectory(src)
#find_package(CURL CONFIG REQUIRED)
# 如果用到了第三方
target_link_libraries(domain_model
    # PUBLIC
    #     CURL::libcurl
)
target_include_directories(domain_model
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)