# bridge库的CMake配置 - 精简版本

# 标准设置 - 根目录已经设置了AUTOMOC
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS 
    Core Widgets Quick Gui Sql Qml Multimedia QuickWidgets Concurrent
)

# 只包含实际需要的头文件，避免链接错误
set(BRIDGE_HEADERS
    #include/BacktestController.h
    include/DataCleaningEngine.h
    include/DataCleaningRuleRegistry.h
    include/DataCleaningPersistence.h
    include/DataManager.h
    include/DataRuleService.h
    include/DataService.h
    include/DataUIController.h
    include/DataServiceCache.h
    include/PreviewDataModel.h
    include/DataFetchController.h
    include/MetaData.h
    include/FactorParamController.h
    include/FactorService.h
    include/FactorViewModel.h
    include/FactorDebugController.h
    include/FactorBacktestController.h
    include/FactorMetaService.h
    include/CleanedDataController.h
    include/MarketDataService.h
    include/RiskConfigService.h
    include/RiskMonitorService.h
    include/PositionAccountService.h
    include/TradeExecutionService.h
    include/TradingConnectionConfigService.h
    include/StrategyBacktestController.h
    include/StrategyService.h
    include/StrategyViewModel.h
)

add_library(bridge STATIC
    ${BRIDGE_HEADERS}
)

add_subdirectory(src)

target_compile_definitions(bridge PRIVATE BUILD_DLL BRIDGE_LIBRARY)

if(ENABLE_JUJIN_MARKET)
    target_compile_definitions(bridge PRIVATE ASTOCK_ENABLE_JUJIN_MARKET=1)
endif()

target_link_libraries(bridge
    PUBLIC
        Qt6::Core
        Qt6::Widgets
        Qt6::Quick
        Qt6::Gui
        Qt6::Multimedia
        Qt6::Sql
        Qt6::Qml
        Qt6::QuickWidgets
        Qt6::Concurrent
    PRIVATE
        engine
        domain
        astock_market
        infrastructure
        domain_model
)

if(ENABLE_JUJIN_MARKET)
    target_link_libraries(bridge PRIVATE thirdParty)
endif()

target_include_directories(bridge
    PUBLIC
        include
    PRIVATE
        src
)

# 输出调试信息
message(STATUS "bridge库头文件: ${BRIDGE_HEADERS}")