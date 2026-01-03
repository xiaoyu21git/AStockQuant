add_library(bridge STATIC)
add_subdirectory(src)
# 如果用到了第三方
find_package(Qt6 REQUIRED COMPONENTS Core  Widgets Quick Gui Sql Qml Multimedia QuickWidgets  )
target_compile_definitions(bridge PRIVATE BUILD_DLL)
target_link_libraries(bridge
    Qt6::Core
    Qt6::Widgets
    Qt6::Quick
    Qt6::Gui
    Qt6::Multimedia 
    Qt6::Sql
    Qt6::Qml
    Qt6::QuickWidgets
 )
target_include_directories(bridge
    PUBLIC
        include
    PRIVATE
        src
        
)
