# 鍥犲瓙绯荤粺CMake閰嶇疆

# 璁剧疆椤圭洰
cmake_minimum_required(VERSION 3.10)
project(FactorSystem)

# 璁剧疆C++鏍囧噯
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckIncludeFiles)
include(CheckFunctionExists)
include(CheckTypeSize)
include(CheckSymbolExists)

# 鍖呭惈鐩綍
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../factor_check/include
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
check_include_files(n.h HAVE_STRING_H)
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

# SIMD flags
include(CheckCXXCompilerFlag)
if(MSVC)
    check_cxx_compiler_flag("/arch:AVX2" HAS_MSVC_AVX2)
    if(HAS_MSVC_AVX2)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/arch:AVX2>)
        add_definitions(-DSIGNAL_SIMD_AVX2 -DSIGNAL_SIMD_SUPPORTED)
    endif()
else()
    check_cxx_compiler_flag("-mavx2" HAS_GCC_AVX2)
    if(HAS_GCC_AVX2)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-mavx2 -mfma>)
        add_definitions(-DSIGNAL_SIMD_AVX2 -DSIGNAL_SIMD_SUPPORTED)
    endif()
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

# 因子系统核心库
set(FACTOR_SOURCES
    src/DataAvailabilityChecker.cpp
    src/BaseFactor.cpp
    src/MomentumFactor.cpp
    src/ValueFactor.cpp
    src/SizeFactor.cpp
    src/LowVolFactor.cpp
    src/QualityFactor.cpp
    src/FactorMetricConfig.cpp
    src/GrowthFactor.cpp
    src/LiquidityFactor.cpp
    src/TechnicalFactor.cpp
    src/DividendFactor.cpp
    src/MacroFactor.cpp
    src/IndustryFactor.cpp
    src/SentimentFactor.cpp
    src/CustomFactor.cpp
    src/CompositeFactor.cpp
    src/FactorConfigAccess.cpp
    src/FactorInstanceManager.cpp
    src/FactorRatingEngine.cpp
    src/FactorTypeRegistry.cpp
    src/FactorBacktestOrchestrator.cpp
    src/CachedMarketDataView.cpp
    src/factor_compute/ArrowMarketDataView.cpp
    src/FactorParamsStubs.cpp
    src/FactorMetricsCalculator.cpp
    src/factor_compute/FactorSignalSetAssembler.cpp
    src/factor_compute/FactorComputeEngine.cpp
    src/factor_compute/FactorRegistry.cpp
    src/factor_compute/FactorOperatorLibrary.cpp
    src/factor_compute/FactorComputeDispatcher.cpp
    src/factor_compute/FactorComputeOperatorExtensions.cpp
    src/factor_compute/PostProcessingPipeline.cpp
    src/factor_compute/SignalCache.cpp
    src/factor_compute/AnalysisKernel.cpp
    src/factor_compute/AnalysisModule.cpp
    src/factor_compute/SimulatedTradingExecutor.cpp
    src/factor_compute/SignalSetBuilder.cpp
    ../factor_check/src/FactorSupportEvaluator.cpp
    ../factor_check/src/FactorSupportInfo.cpp
    ../factor_check/src/FactorSupportScopeCache.cpp
    ../factor_check/src/FactorSupportScopeKey.cpp
)

# 因子系统头文件
set(FACTOR_HEADERS
    include/DataAvailabilityChecker.h
    include/BaseFactor.h
    include/FactorNeutralizationUtils.h
    include/HistoricalView.h
    include/MomentumFactor.h
    include/ValueFactor.h
    include/SizeFactor.h
    include/LowVolFactor.h
    include/QualityFactor.h
    include/FactorMetricConfig.h
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
    include/FactorInstanceManager.h
    include/factor_compute/FactorSignalTypes.h
    include/factor_compute/FactorComputeToken.h
    include/factor_compute/IFactorComputeEngine.h
    include/factor_compute/IFactorSignalSetAssembler.h
    include/factor_compute/IFactorRegistry.h
    include/factor_compute/IFactorComputeDispatcher.h
    include/factor_compute/IFactorComputeOperatorExtension.h
    include/factor_compute/DefaultComputeOpRegistrar.h
    include/factor_compute/ConstComputeOpRegistrar.h
    include/factor_compute/IFactorOperatorLibrary.h
    include/factor_compute/IPostProcessingPipeline.h
    include/factor_compute/IMarketDataView.h
    include/factor_compute/ISignalCache.h
    include/factor_compute/IAnalysisModule.h
    include/factor_compute/FactorComputeDispatcher.h
    include/factor_compute/PostProcessingPipeline.h
    include/factor_compute/FactorSignalSetAssembler.h
    include/factor_compute/SignalCache.h
    include/factor_compute/FactorComputeEngine.h
    include/factor_compute/FactorRegistry.h
    include/factor_compute/FactorOperatorLibrary.h
    include/factor_compute/GroupedBacktestTypes.h
    include/factor_compute/SimulatedTradingExecutor.h
    include/factor_compute/SignalSetBuilder.h
    include/FactorBacktestOrchestrator.h
    include/FactorBacktestTypes.h
    include/FactorMetricsCalculator.h
    include/BacktestRunConfig.h
    ../factor_check/include/factor_check/FactorSupportEvaluator.h
    ../factor_check/include/factor_check/FactorSupportInfo.h
    ../factor_check/include/factor_check/FactorSupportScopeCache.h
    ../factor_check/include/factor_check/FactorSupportScopeKey.h
)

set(FACTOR_COMPUTE_SOURCES
    src/factor_compute/FactorSignalSetAssembler.cpp
    src/factor_compute/FactorComputeEngine.cpp
    src/factor_compute/FactorRegistry.cpp
    src/factor_compute/FactorOperatorLibrary.cpp
    src/factor_compute/FactorComputeDispatcher.cpp
    src/factor_compute/FactorComputeOperatorExtensions.cpp
    src/factor_compute/PostProcessingPipeline.cpp
    src/factor_compute/SignalCache.cpp
    src/factor_compute/AnalysisKernel.cpp
    src/factor_compute/AnalysisModule.cpp
    src/factor_compute/SubMarketDataView.cpp
    src/factor_compute/ParallelChunkScheduler.cpp
    src/factor_compute/FactorSignalAdapter.cpp
    src/factor_compute/SimulatedTradingExecutor.cpp
    src/factor_compute/SignalSetBuilder.cpp
    src/factor_compute/FactorEngine.cpp
    src/factor_compute/MarketDataViewHistoricalAdapter.cpp
)

set(FACTOR_COMPUTE_HEADERS
    include/factor_compute/FactorSignalTypes.h
    include/factor_compute/FactorComputeToken.h
    include/factor_compute/IFactorComputeEngine.h
    include/factor_compute/IFactorSignalSetAssembler.h
    include/factor_compute/IFactorRegistry.h
    include/factor_compute/IFactorComputeDispatcher.h
    include/factor_compute/IFactorComputeOperatorExtension.h
    include/factor_compute/DefaultComputeOpRegistrar.h
    include/factor_compute/ConstComputeOpRegistrar.h
    include/factor_compute/IFactorOperatorLibrary.h
    include/factor_compute/IPostProcessingPipeline.h
    include/factor_compute/IMarketDataView.h
    include/factor_compute/ISignalCache.h
    include/factor_compute/IAnalysisModule.h
    include/factor_compute/AnalysisReportTypes.h
    include/factor_compute/ISignalEngine.h
    include/factor_compute/ParquetMarketDataView.h
    include/factor_compute/ArrowMarketDataView.h
    include/factor_compute/SubMarketDataView.h
    include/factor_compute/FactorComputeDispatcher.h
    include/factor_compute/PostProcessingPipeline.h
    include/factor_compute/FactorSignalSetAssembler.h
    include/factor_compute/SignalCache.h
    include/factor_compute/FactorComputeEngine.h
    include/factor_compute/FactorRegistry.h
    include/factor_compute/FactorOperatorLibrary.h
    include/factor_compute/GroupedBacktestTypes.h
    include/factor_compute/SimulatedTradingExecutor.h
    include/factor_compute/SignalSetBuilder.h
)

# 创建库 — 合并 factor 和 factor_compute 为统一库 factor
set(FACTOR_ALL_SOURCES
    ${FACTOR_SOURCES}
    ${FACTOR_COMPUTE_SOURCES}
)
add_library(factor STATIC ${FACTOR_ALL_SOURCES} ${FACTOR_HEADERS} ${FACTOR_COMPUTE_HEADERS})

target_include_directories(factor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        "${TA_LIB_GENERATED_INCLUDE_DIR}"
        "${TA_LIB_SOURCE_DIR}/include"
        "G:/C++/lib/xxHash-0.8.2"
        "G:/C++/lib/unordered_dense-4.4.0/include"
)

target_link_libraries(factor
    PUBLIC
        Qt6::Sql
        foundation
        infrastructure
        domain_backtest
        domain_scheduler
        domain_trading
        astock_ta_lib
        astock_xxhash
        astock_unordered_dense
)

if(TARGET Arrow::arrow_shared)
    target_link_libraries(factor PUBLIC Arrow::arrow_shared)
    target_include_directories(factor PUBLIC ${arrow_SOURCE_DIR}/cpp/src)
    target_include_directories(factor PUBLIC ${arrow_BINARY_DIR}/src)
elseif(TARGET arrow_shared)
    target_link_libraries(factor PUBLIC arrow_shared)
    target_include_directories(factor PUBLIC ${arrow_SOURCE_DIR}/cpp/src)
    target_include_directories(factor PUBLIC ${arrow_BINARY_DIR}/src)
else()
    message(FATAL_ERROR "Arrow C++ target not found after FetchContent population")
endif()

# 安装
install(TARGETS factor
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include/domain/factor
    FILES_MATCHING PATTERN "*.h"
)

# 娴嬭瘯绋嬪簭
set(FACTOR_TEST_SOURCE "${CMAKE_SOURCE_DIR}/test_factor.cpp")
if(BUILD_TESTS AND EXISTS "${FACTOR_TEST_SOURCE}")
    add_executable(test_factor
        ${FACTOR_TEST_SOURCE}
    )
    
    target_link_libraries(test_factor
        factor
        foundation
        infrastructure
        pymysql
        pthread
    )
    
    # 娣诲姞娴嬭瘯
    add_test(NAME test_factor
        COMMAND test_factor
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()

set(FACTOR_COMPUTE_TEST_SOURCE "${CMAKE_SOURCE_DIR}/tests/test_factor_signal_engine_smoke.cpp")
if(EXISTS "${FACTOR_COMPUTE_TEST_SOURCE}")
    add_executable(test_factor_compute_smoke
        ${FACTOR_COMPUTE_TEST_SOURCE}
    )

    target_link_libraries(test_factor_compute_smoke
        factor
    )

    if(BUILD_TESTS)
        add_test(NAME test_factor_compute_smoke
            COMMAND test_factor_compute_smoke
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        )
    endif()
endif()


