add_library(domain_data STATIC
    # Dead code removed — src/CsvMarketDataSource.cpp deleted as part of market data redesign
)

target_include_directories(domain_data
    PUBLIC
        include
)

target_link_libraries(domain_data
    PUBLIC domain_types
)