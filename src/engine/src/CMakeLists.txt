file(GLOB ENGINE_SOURCES_CXX "*.cxx")
target_sources(engine PRIVATE
    BacktestResult.cpp
    Engineimpl.cpp
    EngineContext.cpp
    ICock.cpp
    Trigger.cpp
    ${ENGINE_SOURCES_CXX}
)