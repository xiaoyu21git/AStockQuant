add_library(domain_types INTERFACE)

target_include_directories(domain_types
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)