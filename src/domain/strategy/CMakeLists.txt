add_library(domain_strategy STATIC)
add_subdirectory(src)

target_include_directories(domain_strategy
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(domain_strategy
    PUBLIC
        domain_types
)

if(TARGET Qt6::Core)
    target_link_libraries(domain_strategy PUBLIC Qt6::Core)
endif()