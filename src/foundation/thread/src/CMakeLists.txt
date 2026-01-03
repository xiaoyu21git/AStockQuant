file(GLOB FOUNDATION_THREAD_SOURCES_CXX "*.cxx")
target_sources(foundation_thread PRIVATE
    ThreadPoolExecutor.cpp
    ${FOUNDATION_THREAD_SOURCES_CXX}
)   