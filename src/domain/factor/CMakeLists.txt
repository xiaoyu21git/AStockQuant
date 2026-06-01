# 因子系统CMake配置

# 设置项目
cmake_minimum_required(VERSION 3.10)
project(FactorSystem)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckIncludeFiles)
include(CheckFunctionExists)
include(CheckTypeSize)
include(CheckSymbolExists)

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/foundation/include
    ${CMAKE_SOURCE_DIR}/src/infrastructure/include
)

set(TA_LIB_LOCAL_SOURCE_DIR_HINTS
    "G:/C++/lib/ta-lib"
    "G:/C++/lib/TA-Lib"
)

unset(TA_LIB_SOURCE_DIR)
foreach(_astock_ta_lib_candidate IN LISTS TA_LIB_LOCAL_SOURCE_DIR_HINTS)
    if(EXISTS "${_astock_ta_lib_candidate}/include/ta_libc.h"
       AND EXISTS "${_astock_ta_lib_candidate}/include/ta_config.h.cmake"
       AND EXISTS "${_astock_ta_lib_candidate}/src/ta_common/ta_global.c"
       AND EXISTS "${_astock_ta_lib_candidate}/src/ta_func")
        set(TA_LIB_SOURCE_DIR "${_astock_ta_lib_candidate}")
        break()
    endif()
endforeach()
unset(_astock_ta_lib_candidate)

if(NOT TA_LIB_SOURCE_DIR)
    message(FATAL_ERROR "Local TA-Lib source tree not found. Download official TA-Lib sources into G:/C++/lib/ta-lib and keep them on disk. Build-time downloads and compatibility shims are disabled.")
endif()

check_include_files(float.h HAVE_FLOAT_H)
check_include_files(inttypes.h HAVE_INTTYPES_H)
check_include_files(limits.h HAVE_LIMITS_H)
check_include_files(locale.h HAVE_LOCALE_H)
check_include_files(stddef.h HAVE_STDDEF_H)
check_include_files(stdint.h HAVE_STDINT_H)
check_include_files(stdlib.h HAVE_STDLIB_H)
check_include_files(string.h HAVE_STRING_H)
check_include_files(unistd.h HAVE_UNISTD_H)
check_include_files(wchar.h HAVE_WCHAR_H)
check_include_files(wctype.h HAVE_WCTYPE_H)
check_function_exists(floor HAVE_FLOOR)
check_function_exists(isascii HAVE_ISASCII)
check_function_exists(localeconv HAVE_LOCALECONV)
check_function_exists(mblen HAVE_MBLEN)
check_function_exists(memmove HAVE_MEMMOVE)
check_function_exists(memset HAVE_MEMSET)
check_function_exists(modf HAVE_MODF)
check_function_exists(pow HAVE_POW)
check_function_exists(sqrt HAVE_SQRT)
check_function_exists(strcasecmp HAVE_STRCASECMP)
check_function_exists(strchr HAVE_STRCHR)
check_function_exists(strerror HAVE_STRERROR)
check_function_exists(strncasecmp HAVE_STRNCASECMP)
check_function_exists(strrchr HAVE_STRRCHR)
check_function_exists(strstr HAVE_STRSTR)
check_function_exists(strtol HAVE_STRTOL)
check_function_exists(strtoul HAVE_STRTOUL)
check_function_exists(strcoll HAVE_STRCOLL)
check_function_exists(strftime HAVE_STRFTIME)
check_function_exists(vprintf HAVE_VPRINTF)
check_type_size(ptrdiff_t SIZEOF_PTRDIFF_T)
if(HAVE_SIZEOF_PTRDIFF_T)
    set(HAVE_PTRDIFF_T 1)
else()
    set(HAVE_PTRDIFF_T 0)
endif()
check_type_size(size_t SIZEOF_SIZE_T)
if(HAVE_SIZEOF_SIZE_T)
    set(HAVE_SIZE_T 1)
else()
    set(HAVE_SIZE_T 0)
    set(size_t "unsigned")
endif()
check_symbol_exists("struct tm" "sys/time.h" TM_IN_SYS_TIME)

set(TA_LIB_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/ta-lib/include")
file(MAKE_DIRECTORY "${TA_LIB_GENERATED_INCLUDE_DIR}")
configure_file(
    "${TA_LIB_SOURCE_DIR}/include/ta_config.h.cmake"
    "${TA_LIB_GENERATED_INCLUDE_DIR}/ta_config.h"
)

file(GLOB TA_LIB_COMMON_SOURCES "${TA_LIB_SOURCE_DIR}/src/ta_common/*.c")
file(GLOB TA_LIB_ABSTRACT_SOURCES "${TA_LIB_SOURCE_DIR}/src/ta_abstract/*.c")
file(GLOB TA_LIB_ABSTRACT_TABLE_SOURCES "${TA_LIB_SOURCE_DIR}/src/ta_abstract/tables/*.c")
file(GLOB TA_LIB_FUNCTION_SOURCES "${TA_LIB_SOURCE_DIR}/src/ta_func/*.c")

add_library(astock_ta_lib STATIC
    ${TA_LIB_COMMON_SOURCES}
    ${TA_LIB_ABSTRACT_SOURCES}
    ${TA_LIB_ABSTRACT_TABLE_SOURCES}
    ${TA_LIB_FUNCTION_SOURCES}
)

if(MSVC)
    target_compile_options(astock_ta_lib PRIVATE /FS)
endif()

target_include_directories(astock_ta_lib
    PUBLIC
        "${TA_LIB_GENERATED_INCLUDE_DIR}"
        "${TA_LIB_SOURCE_DIR}/include"
        "${TA_LIB_SOURCE_DIR}/src/ta_abstract"
        "${TA_LIB_SOURCE_DIR}/src/ta_abstract/frames"
        "${TA_LIB_SOURCE_DIR}/src/ta_common"
        "${TA_LIB_SOURCE_DIR}/src/ta_func"
)

set_source_files_properties(
    ${TA_LIB_COMMON_SOURCES}
    ${TA_LIB_ABSTRACT_SOURCES}
    ${TA_LIB_ABSTRACT_TABLE_SOURCES}
    ${TA_LIB_FUNCTION_SOURCES}
    PROPERTIES LANGUAGE C
)

# 源文件
set(FACTOR_SOURCES
    src/DataAvailabilityChecker.cpp
    src/ArrowMarketData.cpp
    src/BaseFactor.cpp
    src/batch_technical_indicators.cpp
    src/MomentumFactor.cpp
    src/ValueFactor.cpp
    src/SizeFactor.cpp
    src/LowVolFactor.cpp
    src/QualityFactor.cpp
    src/ConfigurableFactor.cpp
    src/ConfigurableFactorParams.cpp
    src/ConfigurableFactorDetail.cpp
    src/GrowthFactor.cpp
    src/LiquidityFactor.cpp
    src/TechnicalFactor.cpp
    src/DividendFactor.cpp
    src/MacroFactor.cpp
    src/IndustryFactor.cpp
    src/SentimentFactor.cpp
    src/CustomFactor.cpp
    src/CompositeFactor.cpp
    src/ConfigurableFactorGrowth.cpp
    src/ConfigurableFactorLiquidity.cpp
    src/ConfigurableFactorTechnical.cpp
    src/ConfigurableFactorDividend.cpp
    src/ConfigurableFactorMacro.cpp
    src/ConfigurableFactorIndustry.cpp
    src/ConfigurableFactorSentiment.cpp
    src/ConfigurableFactorCustom.cpp
    src/FactorConfigAccess.cpp
    src/FactorCacheManager.cpp
    src/FactorInstanceManager.cpp
    src/FactorBacktestMetricsCalculator.cpp
    src/FactorBacktestExecutor.cpp
)

# 头文件
set(FACTOR_HEADERS
    include/ArrowMarketData.h
    include/DataAvailabilityChecker.h
    include/BaseFactor.h
    include/FactorNeutralizationUtils.h
    include/batch_technical_indicators.h
    include/HistoricalView.h
    include/MomentumFactor.h
    include/ValueFactor.h
    include/SizeFactor.h
    include/LowVolFactor.h
    include/QualityFactor.h
    include/ConfigurableFactor.h
    include/GrowthFactor.h
    include/LiquidityFactor.h
    include/TechnicalFactor.h
    include/DividendFactor.h
    include/MacroFactor.h
    include/IndustryFactor.h
    include/SentimentFactor.h
    include/CustomFactor.h
    include/CompositeFactor.h
    include/CompositeFactorConfig.h
    include/IFactorResolver.h
    include/FactorConfigAccess.h
    include/FactorCacheManager.h
    include/FactorInstanceManager.h
    include/FactorBacktestMetricsCalculator.h
    include/FactorBacktestExecutor.h
)

# 创建库
add_library(factor_system STATIC ${FACTOR_SOURCES} ${FACTOR_HEADERS})

target_include_directories(factor_system
    PUBLIC
    "${TA_LIB_GENERATED_INCLUDE_DIR}"
    "${TA_LIB_SOURCE_DIR}/include"
)

# 链接库
target_link_libraries(factor_system
    PUBLIC
    Qt6::Sql
        foundation
        infrastructure
        domain_backtest
    domain_trading
        astock_ta_lib
)

if(TARGET Arrow::arrow_shared)
    target_link_libraries(factor_system PUBLIC Arrow::arrow_shared)
    target_include_directories(factor_system PUBLIC ${arrow_SOURCE_DIR}/cpp/src)
    target_include_directories(factor_system PUBLIC ${arrow_BINARY_DIR}/src)
elseif(TARGET arrow_shared)
    target_link_libraries(factor_system PUBLIC arrow_shared)
    target_include_directories(factor_system PUBLIC ${arrow_SOURCE_DIR}/cpp/src)
    target_include_directories(factor_system PUBLIC ${arrow_BINARY_DIR}/src)
else()
    message(FATAL_ERROR "Arrow C++ target not found after FetchContent population")
endif()

# 安装
install(TARGETS factor_system
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include/domain/factor
    FILES_MATCHING PATTERN "*.h"
)

# 测试程序
set(FACTOR_TEST_SOURCE "${CMAKE_SOURCE_DIR}/test_factor_system.cpp")
if(BUILD_TESTS AND EXISTS "${FACTOR_TEST_SOURCE}")
    add_executable(test_factor_system
        ${FACTOR_TEST_SOURCE}
    )
    
    target_link_libraries(test_factor_system
        factor_system
        foundation
        infrastructure
        pymysql
        pthread
    )
    
    # 添加测试
    add_test(NAME test_factor_system
        COMMAND test_factor_system
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()