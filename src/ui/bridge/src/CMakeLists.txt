file(GLOB BRIDGE_SOURCES_CXX "*.cxx")
target_sources(bridge PRIVATE
    TradeRecordModel.cpp
    EquityCurveModel.cpp
    BacktestController.cpp
    HighPositionModel.cpp
    LiveAccountModel.cpp
    LiveActionLogModel.cpp
    LivePositionModel.cpp
    GlobalState.cpp
    registerQmlTypes.cpp
    ../moc/GlobalState.cpp
    ${BRIDGE_SOURCES_CXX}
)