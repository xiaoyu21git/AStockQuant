# bridge库的源文件 - 精简版本
# 只包含实际需要的文件，避免链接错误

# 核心模型文件 - 精简版，移除旧数据服务组件
set(BRIDGE_SOURCES
    CleanedDataController.cpp     # 新增：清洗数据控制器
    DataCleaningEngine.cpp
    DataCleaningRuleRegistry.cpp
    DataCleaningPersistence.cpp  # 新增：数据清洗持久化服务
    DataManager.cpp
    DataUIController.cpp
    DataService.cpp           # 新增：极简DataService
    DataRuleService.cpp       # 新增：规则服务
    DataServiceCache.cpp  
    PreviewDataModel.cpp    # 新增：缓存服务
    registerQmlTypes.cpp
    DataFetchController.cpp   # 新增：DataFetchController
    MetaData.cpp              # 新增：元数据配置系统
    FactorParamController.cpp # 新增：因子参数控制器
    FactorService.cpp         # 新增：因子服务
    FactorViewModel.cpp       # 新增：因子视图模型
    FactorDebugController.cpp # 新增：因子调试控制器
    FactorBacktestController.cpp # 新增：因子回测控制器
    FactorMetaService.cpp     # 新增：因子元数据服务
    RiskConfigService.cpp     # 新增：风险配置服务
    RiskMonitorService.cpp    # 新增：风险监控快照服务
    StrategyBacktestController.cpp # 新增：策略回测控制器
    StrategyService.cpp       # 新增：策略服务
    StrategyViewModel.cpp     # 新增：策略视图模型
)

# 显式指定源文件，避免自动包含其他.cpp文件
target_sources(bridge PRIVATE ${BRIDGE_SOURCES})

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