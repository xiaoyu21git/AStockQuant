add_library(domain_scheduler INTERFACE)

target_include_directories(domain_scheduler
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(domain_scheduler INTERFACE cxx_std_17)