# astock_rule_engine — 独立 C++ 规则评估引擎
# 零 Qt 依赖, 纯 C++17
# 公开依赖: foundation (JsonFacade + logging)
# 内部依赖: factor, domain_strategies, astock_market, astock_ta_lib

# ── 生成内置规则嵌入源文件 ──
set(RULES_BUILTIN_CPP "${CMAKE_CURRENT_BINARY_DIR}/rules_builtin.cpp")

# glob 所有模板 JSON 文件作为依赖
file(GLOB RULES_TEMPLATE_JSONS "${CMAKE_SOURCE_DIR}/config/rules/templates/*.json")

add_custom_command(
    OUTPUT ${RULES_BUILTIN_CPP}
    COMMAND python
        "${CMAKE_SOURCE_DIR}/tools/embed_rules.py"
        --output "${RULES_BUILTIN_CPP}"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/embed_rules.py"
        "${CMAKE_SOURCE_DIR}/config/rules/catalog.json"
        ${RULES_TEMPLATE_JSONS}
    COMMENT "Embedding built-in rules into C++ source"
)

add_library(astock_rule_engine STATIC
    RuleConditionEvaluator.cpp
    RuleVariableProvider.cpp
    RuleGate.cpp
    RuleAttribution.cpp
    RuleLibrary.cpp
    RuleRepositories.cpp
    ../src/RulePipeline.cpp
    ${RULES_BUILTIN_CPP}
)

target_include_directories(astock_rule_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}              # RuleTypes.h, RuleGate.h, RuleConditionEvaluator.h, RuleVariableProvider.h, RuleAttribution.h, RuleLibrary.h, RuleRepositories.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../include   # RulePipeline.h
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_BINARY_DIR}              # rules_builtin.cpp 生成目录
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
