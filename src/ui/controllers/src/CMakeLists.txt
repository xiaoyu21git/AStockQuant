file(GLOB CONTROLLERS_SOURCES_CXX "*.cxx")
target_sources(contollers PRIVATE
    registerQmlTypes.cpp
    ${CONTROLLERS_SOURCES_CXX}
)