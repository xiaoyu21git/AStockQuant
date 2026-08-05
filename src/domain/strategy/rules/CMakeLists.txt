# astock_rule_engine — 独立 C++ 规则评估引擎
# 零 Qt 依赖, 纯 C++17
# 公开依赖: foundation (JsonFacade + logging)
# 内部依赖: factor, domain_strategies, astock_market, astock_ta_lib

# ═══════════════════════════════════════════════════════════════════
# 规则编译流水线 (全自动, 零手动脚本)
#   YAML 变更 → CMake 自动检测 → compile_rules.py → compiled.json
#                                  → embed_rules.py → rules_builtin.cpp
#                                  → MSVC → astock_rule_engine.lib → EXE
# ═══════════════════════════════════════════════════════════════════

set(COMPILED_JSON     "${CMAKE_SOURCE_DIR}/config/rules/compiled.json")
set(RULES_BUILTIN_CPP "${CMAKE_CURRENT_BINARY_DIR}/rules_builtin.cpp")
set(CATALOG_YAML      "${CMAKE_SOURCE_DIR}/astock_engine/rules/catalogs/trading_term_catalog.yaml")

file(GLOB_RECURSE RULE_YAML_SOURCES "${CMAKE_SOURCE_DIR}/astock_engine/rules/examples/*.yaml")

# ── 第一步: YAML → compiled.json ──
add_custom_command(
    OUTPUT ${COMPILED_JSON}
    COMMAND python
        "${CMAKE_SOURCE_DIR}/tools/compile_rules.py"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/compile_rules.py"
        ${CATALOG_YAML}
        ${RULE_YAML_SOURCES}
    COMMENT "编译规则: YAML → compiled.json"
)

# ── 第二步: compiled.json → rules_builtin.cpp ──
add_custom_command(
    OUTPUT ${RULES_BUILTIN_CPP}
    COMMAND python
        "${CMAKE_SOURCE_DIR}/tools/embed_rules.py"
        --output "${RULES_BUILTIN_CPP}"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/embed_rules.py"
        ${COMPILED_JSON}
    COMMENT "嵌入规则: compiled.json → rules_builtin.cpp"
)

add_library(astock_rule_engine STATIC
    RuleConditionEvaluator.cpp
    RuleVariableProvider.cpp
    RuleGate.cpp
    RuleAttribution.cpp
    RuleLibrary.cpp
    RuleRepositories.cpp
    RulePatchLoader.cpp
    ../src/RulePipeline.cpp
    ${RULES_BUILTIN_CPP}
)

target_include_directories(astock_rule_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/src/domain/factor/include
        ${CMAKE_SOURCE_DIR}/src/domain/trading
        ${CMAKE_SOURCE_DIR}/src/domain/trading/include
        ${CMAKE_SOURCE_DIR}/src/domain/market/include
)

target_link_libraries(astock_rule_engine
    PUBLIC
        foundation
    PRIVATE
        factor
        domain_strategies
        astock_market
        astock_ta_lib
)

if (MSVC)
    target_compile_options(astock_rule_engine PRIVATE /permissive-)
endif()
