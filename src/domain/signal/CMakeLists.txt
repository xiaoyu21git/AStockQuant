# signal/CMakeLists.txt
add_library(domain_signal STATIC)
add_subdirectory(src)

set_target_properties(domain_signal PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(domain_signal PRIVATE "/Zc:__cplusplus" "/permissive-")
endif()

target_link_libraries(domain_signal
    PRIVATE
        foundation
)

target_include_directories(domain_signal
    PUBLIC
        include
    PRIVATE
        src
)
