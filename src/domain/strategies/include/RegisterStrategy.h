#pragma once
#include "StrategyFactory.h"

#define REGISTER_STRATEGY(CLASS_NAME) \
namespace { \
struct CLASS_NAME##Registrar { \
    CLASS_NAME##Registrar() { \
        domain::StrategyFactory::instance().registerStrategy(#CLASS_NAME, [](){ return std::make_shared<CLASS_NAME>(); }); \
    } \
}; \
static CLASS_NAME##Registrar global_##CLASS_NAME##Registrar; \
}
