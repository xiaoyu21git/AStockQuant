# src/CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(astock_src VERSION 0.0.1 LANGUAGES CXX)

# ==================== Qt鍖呭惈 ====================
include_directories(${Qt6Core_INCLUDE_DIRS} ${Qt6Widgets_INCLUDE_DIRS})

# ==================== 娣诲姞瀛愮洰褰曪紙纭繚渚濊禆椤哄簭锛?====================
# 鍏堟瀯寤哄熀纭€搴?
add_subdirectory(foundation)

# 缂撳瓨搴撲緷璧栧熀纭€搴?
add_subdirectory(cache)

# 棰嗗煙妯″瀷渚濊禆鍩虹搴?
add_subdirectory(domain)

# 鍩虹璁炬柦灞備緷璧?foundation / domain_types
add_subdirectory(infrastructure)

# 新应用合同层依赖领域与基础设施
add_subdirectory(application)

# 寮曟搸渚濊禆棰嗗煙妯″瀷鍜屽熀纭€搴?
add_subdirectory(engine)

# 绗笁鏂硅鎯呴€傞厤鎸夐渶鍚敤锛岄伩鍏嶇己灏?SDK import lib 鏃跺奖鍝嶉粯璁ゆ瀯寤?
if(ENABLE_JUJIN_MARKET)
    set(GMSDK_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/gmsdk")

    if(NOT DEFINED GMSDK_LIBRARY OR GMSDK_LIBRARY STREQUAL "")
        set(GMSDK_LIBRARY "${GMSDK_ROOT}/lib/win64/gmsdk.lib" CACHE FILEPATH "Path to GM SDK import library")
    endif()

    if(NOT EXISTS "${GMSDK_LIBRARY}")
        message(FATAL_ERROR "ENABLE_JUJIN_MARKET=ON but GMSDK_LIBRARY was not found: ${GMSDK_LIBRARY}")
    endif()

    add_library(thirdParty INTERFACE)
    target_include_directories(thirdParty INTERFACE "${GMSDK_ROOT}")
    target_link_libraries(thirdParty INTERFACE "${GMSDK_LIBRARY}")
endif()

# UI 渚濊禆鍓嶉潰鐨勫簱
add_subdirectory(ui)

# 搴旂敤渚濊禆鎵€鏈?
add_subdirectory(app)

# ==================== 鍒涘缓涓诲簱鐩爣锛堜緵Python缁戝畾浣跨敤锛?====================
# 鏀堕泦鎵€鏈夊簱鏂囦欢
set(ALL_LIBS foundation cache_module domain engine infrastructure application_contracts)

# 鍒涘缓鎺ュ彛搴擄紝鏂逛究Python缁戝畾閾炬帴
add_library(astock_core INTERFACE)
target_link_libraries(astock_core INTERFACE ${ALL_LIBS})
target_include_directories(astock_core INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Qt6Core_INCLUDE_DIRS}
    ${Qt6Widgets_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/thirdparty/gmsdk
)


