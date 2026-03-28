#pragma once

#include <string>

#include "foundation/json/json_facade.h"

namespace factor {
namespace json_helper {

inline foundation::json::JsonFacade toJsonValue(const std::string& value) {
    return foundation::json::JsonFacade::createString(value);
}

inline foundation::json::JsonFacade toJsonValue(const char* value) {
    return foundation::json::JsonFacade::createString(value == nullptr ? std::string() : std::string(value));
}

inline foundation::json::JsonFacade toJsonValue(int value) {
    return foundation::json::JsonFacade::createInt(value);
}

inline foundation::json::JsonFacade toJsonValue(double value) {
    return foundation::json::JsonFacade::createDouble(value);
}

inline foundation::json::JsonFacade toJsonValue(bool value) {
    return foundation::json::JsonFacade::createBool(value);
}

} // namespace json_helper
} // namespace factor