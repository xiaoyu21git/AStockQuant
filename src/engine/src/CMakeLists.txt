file(GLOB ENGINE_SOURCES_CXX "*.cxx")
target_sources(engine PRIVATE
    # DataManager.cpp
    # CacheManager.cpp
    BacktestEngine.cpp
    ${ENGINE_SOURCES_CXX}
)