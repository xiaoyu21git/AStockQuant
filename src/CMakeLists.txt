# src/CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(astock_src VERSION 0.0.1 LANGUAGES CXX)

# ==================== 添加子目录（确保依赖顺序） ====================
# 先构建基础库
add_subdirectory(foundation)

# 领域模型依赖基础库
add_subdirectory(domain)

# 引擎依赖领域模型和基础库
add_subdirectory(engine)

# 可选 UI 与应用（依赖 Qt），在 CI 上通常禁用
if(BUILD_UI)
    include_directories(${Qt6Core_INCLUDE_DIRS} ${Qt6Widgets_INCLUDE_DIRS})
    add_subdirectory(ui)
    add_subdirectory(app)
endif()

# ==================== 创建主库目标（供Python绑定使用） ====================
# 收集所有库文件（UI/app 不在测试所需库中）
set(ALL_LIBS foundation domain engine)

# 创建接口库，方便Python绑定链接
add_library(astock_core INTERFACE)
target_link_libraries(astock_core INTERFACE ${ALL_LIBS})
target_include_directories(astock_core INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}
)