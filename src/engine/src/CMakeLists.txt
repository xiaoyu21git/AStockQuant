file(GLOB ENGINE_SOURCES_CXX "*.cxx")
target_sources(engine PRIVATE
    BacktestResult.cpp
    DataSource.cpp
    Engineimpl.cpp
    EngineContext.cpp
    GlobalEventBusRegistry.cpp
    AccountEngine.cpp
    GmSessionEngine.cpp
    OrderManager.cpp
    TradeEngine.cpp
    Event/Event.cpp
    Event/DispatchWorker.cpp
    Event/EventBusImpl.cpp
    Event/EventFormat.cpp
   # Event/EventQueue.cpp
    ICock.cpp
    Trigger.cpp
    ${ENGINE_SOURCES_CXX}
)