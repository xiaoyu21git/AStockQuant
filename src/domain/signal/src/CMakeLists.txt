# signal/src/CMakeLists.txt
target_sources(domain_signal PRIVATE
    SignalFormatter.cpp
    FileSignalPusher.cpp
    SignalListener.cpp
)

set_target_properties(domain_signal PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_include_directories(domain_signal PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
