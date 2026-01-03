add_library(engine STATIC)

add_subdirectory(src)
# target_sources(engine
#     PRIVATE
#         # DataManager.cpp
#         # CacheManager.cpp
# )
target_link_libraries(engine
    PUBLIC
        domain
        # domain_model
        # domain_signals
)
target_include_directories(engine
    PUBLIC
        include
    PRIVATE
        src
)