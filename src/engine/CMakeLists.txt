add_library(astock_engine STATIC)

target_sources(astock_engine
    PRIVATE
        DataManager.cpp
        CacheManager.cpp
)

target_include_directories(astock_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(astock_engine
    PUBLIC
        astock_domain
    PRIVATE
        astock_foundation
)
