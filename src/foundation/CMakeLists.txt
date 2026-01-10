
add_library(foundation STATIC)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE) # Windows 必须
set(LIBS_ROOT "C:/libs")
find_package(CURL CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
add_subdirectory(src)
# target_sources(foundation
#     PRIVATE
#         src/YamlManager.cpp
# )

add_executable(foundation_test
    tests/test_random.cpp
    tests/test_thread_pool.cpp
    tests/test_utils.cpp
    tests/test_main.cpp
    tests/test_timeutils.cpp

)
target_include_directories(foundation_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LIBS_ROOT}/nlohmann_json/include
    ${LIBS_ROOT}/GTest/include
)
# 链接依赖
target_link_libraries(foundation_test
    foundation
    C:/libs/GTest/lib/gtest.lib 
    C:/libs/GTest/lib/gtest_main.lib
)

# 如果用到了第三方
target_link_libraries(foundation
    PRIVATE
        CURL::libcurl
        yaml-cpp
)
target_include_directories(foundation
    PUBLIC
        include
        "${LIBS_ROOT}/nlohmann_json/include"
    PRIVATE
        src
)
# 手动添加测试（兼容旧版本）
enable_testing()
add_test(NAME foundation_test COMMAND foundation_test)

# 可选：添加更多的测试分组
add_test(NAME random_test COMMAND foundation_test --gtest_filter=RandomTest*)
add_test(NAME thread_pool_test COMMAND foundation_test --gtest_filter=ThreadPoolTest*)
add_test(NAME utils_test COMMAND foundation_test --gtest_filter=*UtilsTest*)

# Windows 特定设置
if(WIN32)
    target_compile_definitions(foundation_test PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
    )
    
    # 如果是 Debug 构建
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_definitions(foundation_test PRIVATE
            _ITERATOR_DEBUG_LEVEL=0
        )
    endif()
endif()
#add_subdirectory(test)