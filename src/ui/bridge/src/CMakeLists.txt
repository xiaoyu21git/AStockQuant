# bridge库的源文件 - 精简版本
# 只包含实际需要的文件，避免链接错误

# 核心模型文件 - 精简版，移除旧数据服务组件
set(BRIDGE_SOURCES
    #BacktestController.cpp
    DataCleaningEngine.cpp
    DataManager.cpp
    DataUIController.cpp
    DataService.cpp           # 新增：极简DataService
    DataSourceService.cpp     # 新增：数据源服务
    DataRuleService.cpp       # 新增：规则服务
    DataPreviewService.cpp    # 新增：预览服务
    DataCleaningService_refactored.cpp  # 重构版本：使用foundation线程池
    DataServiceCache.cpp  
    PreviewDataModel.cpp    # 新增：缓存服务
    registerQmlTypes.cpp
    DataFetchController.cpp   # 新增：DataFetchController
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