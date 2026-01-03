# add_library(foundation_json STATIC)
# set(CMAKE_CXX_STANDARD 17)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)
# SET(CMAKE_AUTOMOC ON)
# SET(CMAKE_AUTORCC ON)
# add_subdirectory(src)
set(LIBS_ROOT "C:/libs")
# include_directories(
#     "${LIBS_ROOT}/nlohmann_json/include"
# )
# # ========== 头文件目录 ==========
# # include_directories(
# #     "${LIBS_ROOT}/nlohmann_json"
# # )
# target_include_directories(foundation_json
#     PUBLIC
#         "${LIBS_ROOT}/nlohmann_json/include"
#     PRIVATE
       
# )
# target_include_directories(foundation_json
#     PUBLIC
#         include
#     PRIVATE
#         src
# )

add_library(foundation_json STATIC)

# target_sources(foundation_json
#     PRIVATE
#         src/JsonFacade.cpp
# )
add_subdirectory(src)
target_include_directories(foundation_json
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        "${LIBS_ROOT}/nlohmann_json/include"
)

target_link_libraries(foundation_json
    PUBLIC
        foundation_public
        #nlohmann_json
)