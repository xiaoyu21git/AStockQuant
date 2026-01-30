# src/CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(astock_src VERSION 0.0.1 LANGUAGES CXX)

# ==================== Qt包含 ====================
include_directories(${Qt6Core_INCLUDE_DIRS} ${Qt6Widgets_INCLUDE_DIRS})

# ==================== 添加子目录（确保依赖顺序） ====================
# 先构建基础库
add_subdirectory(foundation)

# 领域模型依赖基础库
add_subdirectory(domain)

# 引擎依赖领域模型和基础库
add_subdirectory(engine)
#add_subdirectory(infrastructure)
# # UI依赖所有前面的库
add_subdirectory(ui)

# 应用依赖所有
add_subdirectory(app)

# ==================== 创建主库目标（供Python绑定使用） ====================
# 收集所有库文件
set(ALL_LIBS foundation domain engine)

# 创建接口库，方便Python绑定链接
add_library(astock_core INTERFACE)
target_link_libraries(astock_core INTERFACE ${ALL_LIBS})
target_include_directories(astock_core INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Qt6Core_INCLUDE_DIRS}
    ${Qt6Widgets_INCLUDE_DIRS}
)