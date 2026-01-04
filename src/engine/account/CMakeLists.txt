
add_library(engine_account STATIC)
add_subdirectory(src)
target_include_directories(engine_account
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(engine_account
    PUBLIC domain_model
)