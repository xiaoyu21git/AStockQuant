add_library(astock_domain STATIC)

target_sources(astock_domain
    PRIVATE
        strategies/IStrategy.h
        signals/SignalTypes.h
)

target_include_directories(astock_domain
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# ❗domain 不允许依赖 Qt / curl / json
