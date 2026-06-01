add_library(domain_trading STATIC)
add_subdirectory(src)

target_include_directories(domain_trading
    PUBLIC
        include
    PRIVATE
        src
)

target_link_libraries(domain_trading
    PUBLIC
        foundation
)

if(TARGET Qt6::Core)
    target_link_libraries(domain_trading PUBLIC Qt6::Core)
endif()

set_target_properties(domain_trading PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)