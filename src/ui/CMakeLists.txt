#add_library(astock_ui STATIC)

# target_sources(astock_ui
#     PRIVATE
       
# )
add_subdirectory(bridge)  # 引入 bridge 模块
add_subdirectory(controllers)  # 引入 controllers模块
# target_include_directories(astock_ui
#     PUBLIC
#         ${CMAKE_CURRENT_SOURCE_DIR}
# )

# target_link_libraries(astock_ui
#     PRIVATE
#         astock_engine
#         Qt6::Core
#         Qt6::Gui
#         Qt6::Qml
#         Qt6::Quick
# )
