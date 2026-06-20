add_library(domain_cleaning STATIC
    include/DataCache.h
    include/DataFieldKeys.h
    include/ICleaningRule.h
    include/CleaningEngine.h
    include/rules/CoreCleaningRules.h
)

# Parquet 持久化实现（仅在 Arrow 可用时编译）
if(TARGET Arrow::arrow_shared)
    target_sources(domain_cleaning PRIVATE src/DataCacheParquet.cpp)
    target_link_libraries(domain_cleaning PUBLIC Arrow::arrow_shared)
    target_include_directories(domain_cleaning PUBLIC ${arrow_SOURCE_DIR}/cpp/src)
    target_include_directories(domain_cleaning PUBLIC ${arrow_BINARY_DIR}/src)
    target_compile_definitions(domain_cleaning PUBLIC ASTOCK_HAS_PARQUET)
    message(STATUS "[domain_cleaning] Parquet storage enabled")
elseif(TARGET arrow_shared)
    target_sources(domain_cleaning PRIVATE src/DataCacheParquet.cpp)
    target_link_libraries(domain_cleaning PUBLIC arrow_shared)
    target_include_directories(domain_cleaning PUBLIC ${arrow_SOURCE_DIR}/cpp/src)
    target_include_directories(domain_cleaning PUBLIC ${arrow_BINARY_DIR}/src)
    target_compile_definitions(domain_cleaning PUBLIC ASTOCK_HAS_PARQUET)
    message(STATUS "[domain_cleaning] Parquet storage enabled (arrow_shared)")
else()
    message(WARNING "[domain_cleaning] Arrow not available — Parquet disabled, using JSON fallback")
endif()

target_include_directories(domain_cleaning PUBLIC include)
target_link_libraries(domain_cleaning PUBLIC foundation astock_unordered_dense)
