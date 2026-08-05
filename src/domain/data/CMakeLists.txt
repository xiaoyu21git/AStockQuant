add_library(domain_data STATIC
    src/DataTableAssembler.cpp
)

target_include_directories(domain_data
    PUBLIC
        include
)

target_link_libraries(domain_data
    PUBLIC
        domain_types
        foundation
        infrastructure
)

if(TARGET Arrow::arrow_shared)
    target_link_libraries(domain_data PUBLIC Arrow::arrow_shared)
    target_include_directories(domain_data PUBLIC ${arrow_SOURCE_DIR}/cpp/src)
    target_include_directories(domain_data PUBLIC ${arrow_BINARY_DIR}/src)
elseif(TARGET arrow_shared)
    target_link_libraries(domain_data PUBLIC arrow_shared)
    target_include_directories(domain_data PUBLIC ${arrow_SOURCE_DIR}/cpp/src)
    target_include_directories(domain_data PUBLIC ${arrow_BINARY_DIR}/src)
else()
    message(FATAL_ERROR "Arrow C++ target not found")
endif()
