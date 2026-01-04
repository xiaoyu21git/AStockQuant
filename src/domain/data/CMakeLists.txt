add_library(domain_data STATIC
    src/CsvMarketDataSource.cpp
)

target_include_directories(domain_data
    PUBLIC
        include
)

target_link_libraries(domain_data
    PUBLIC domain_model
)

