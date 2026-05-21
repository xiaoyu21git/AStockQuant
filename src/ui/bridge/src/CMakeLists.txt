# bridge库的源文件 - 精简版本
# 只包含实际需要的文件，避免链接错误

# 核心模型文件 - 精简版，移除旧数据服务组件
set(BRIDGE_SOURCES
    ../include/RuleTemplateSuggestionService.h
    ../include/RuleTemplateDetailHelper.h
        CleanedDataController.cpp
    cleaning/CleaningEngine.cpp
    DataManager.cpp
    DataUIController.cpp
    DataService.cpp           # 新增：极简DataService
    DataRuleService.cpp       # 新增：规则服务
    DataServiceCache.cpp  
    CacheDetailPreviewModel.cpp
    PreviewDataModel.cpp    # 新增：缓存服务
    registerQmlTypes.cpp
    DataFetchController.cpp   # 新增：DataFetchController
    MetaData.cpp              # 新增：元数据配置系统
    ExecutionSchedulingRuleChain.cpp
    FactorService.cpp         # 新增：因子服务
    FactorViewModel.cpp       # 新增：因子视图模型
    FactorDebugController.cpp # 新增：因子调试控制器
    FactorBacktestResultContract.cpp
    FactorDetectionService.cpp
    FactorBacktestController.cpp # 新增：因子回测控制器
    FactorMetaService.cpp     # 新增：因子元数据服务
    MarketDataService.cpp     # 新增：行情桥接服务
    PortfolioAnalysisService.cpp # 新增：组合分析服务
    OrderRuntimeUtils.cpp     # 新增：订单状态公共工具
    RuleTemplateDetailHelper.cpp
    RuleTemplateRuntimeEvaluator.cpp
    RiskConfigService.cpp     # 新增：风险配置服务
    RiskMonitorService.cpp    # 新增：风险监控快照服务
    PositionAccountService.cpp # 新增：持仓账户服务
    TradeExecutionService.cpp # 新增：交易执行服务
    TradingConnectionConfigService.cpp # 新增：交易连接配置服务
    TradingMarketCalendarService.cpp # 新增：交易市场日历桥接服务
    TradingRuntimeStatusService.cpp # 新增：交易运行时状态桥接服务
    UiLifecycleCoordinator.cpp
    RuleTemplateSuggestionService.cpp # 新增：规则模板建议桥接服务
    StrategyBacktestController.cpp # 新增：策略回测控制器
    StrategyRuntimeRuleEvaluator.cpp
    StrategyStructureResolvers.cpp
    StrategyService.cpp       # 新增：策略服务
    StrategyViewModel.cpp     # 新增：策略视图模型
)

if(ENABLE_JUJIN_MARKET)
    list(APPEND BRIDGE_SOURCES
        JujinApi.cpp              # 新增：掘金 API 外观层
        TradingCommandQueue.cpp   # 新增：交易命令队列
        GmStrategySession.cpp     # 新增：GM Strategy 会话运行时
        TradingRuntimeManager.cpp # 新增：交易运行时管理器
    )
endif()

list(TRANSFORM BRIDGE_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

set(BRIDGE_SOURCES ${BRIDGE_SOURCES} PARENT_SCOPE)

# 添加包含目录
target_include_directories(bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Qt自动处理moc，不需要手动添加moc文件
set_target_properties(bridge PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC ON
)

# 链接Qt库
target_link_libraries(bridge PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Quick
    Qt6::Qml
    Qt6::Sql
    Qt6::Gui
)

# 链接基础设施库
target_link_libraries(bridge PRIVATE
    infrastructure
    engine
    foundation
    cache_module
)

# 输出调试信息
message(STATUS "bridge库源文件: ${BRIDGE_SOURCES}")
message(STATUS "bridge库包含目录: ${CMAKE_CURRENT_SOURCE_DIR}/../include")
