add_library(domain_types INTERFACE)

target_include_directories(domain_types
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(TARGET Qt6::Core)
    target_link_libraries(domain_types
        INTERFACE
            Qt6::Core
    )
endif()