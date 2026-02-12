file(GLOB BRIDGE_SOURCES_CXX "*.cxx")
#file(GLOB BRIDGE_HEADERS "../include/*.h")

target_sources(bridge PRIVATE
    TradeRecordModel.cpp
    EquityCurveModel.cpp
    BacktestController.cpp
    HighPositionModel.cpp
    LiveAccountModel.cpp
    LiveActionLogModel.cpp
    LivePositionModel.cpp
    GlobalState.cpp
    DataFetchController.cpp
    DataCleaningEngine.cpp
    DataService.cpp
    CleaningResultModel.cpp
    DataManager.cpp
    registerQmlTypes.cpp
)
