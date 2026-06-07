add_library(domain_scheduler STATIC)

target_sources(domain_scheduler PRIVATE
    src/BacktestScheduler.cpp
)

target_include_directories(domain_scheduler
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(domain_scheduler
    PUBLIC
        factor
        factor
)

target_compile_features(domain_scheduler PUBLIC cxx_std_17)

