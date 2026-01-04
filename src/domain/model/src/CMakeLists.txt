file(GLOB DOMAIN_MODEL_SOURCES_CXX "*.cxx")
target_sources(domain_model PRIVATE
    Model.cpp
    BarProvider.cpp
    #Net_facade.cpp
    ${DOMAIN_MODEL_SOURCES_CXX}
)