file(GLOB ENGINE_SOURCES_CXX "*.cxx")
target_sources(engine PRIVATE
    BacktestResult.cpp
    DataSource.cpp
    Engineimpl.cpp
    EngineContext.cpp
    Event.cpp
    DispatchWorker.cpp
    EventDispatcher.cpp
    EventBusImpl.cpp
    EventQueue.cpp
    ICock.cpp
    Trigger.cpp
    ${ENGINE_SOURCES_CXX}
)