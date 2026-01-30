#include "EventValue.h"
#include <stdexcept>

// ===== 静态工厂方法实现 =====

EventValue EventValue::from_json(const foundation::json::JsonFacade& json) {
    if (json.isString()) {
        return EventValue::from_string(json.asString());
    }
    else if (json.isInt()) {
        return EventValue::from_int(static_cast<int64_t>(json.asInt()));
    }
    else if (json.isDouble()) {
        return EventValue::from_double(json.asDouble());
    }
    else if (json.isBool()) {
        return EventValue::from_bool(json.asBool());
    }
    else if (json.isArray()) {
        auto arr = json.asArray();
        if (arr.empty()) {
            // 空数组默认返回字符串数组
            return EventValue::from_string_vector({});
        }
        
        // 检查第一个元素类型
        const auto& first = arr[0];
        if (first.isString()) {
            std::vector<std::string> string_arr;
            string_arr.reserve(arr.size());
            for (const auto& item : arr) {
                if (!item.isString()) {
                    throw std::runtime_error("Mixed types in string array");
                }
                string_arr.push_back(item.asString());
            }
            return EventValue::from_string_vector(string_arr);
        }
        else if (first.isInt() || first.isDouble()) {
            std::vector<double> double_arr;
            double_arr.reserve(arr.size());
            for (const auto& item : arr) {
                if (item.isInt()) {
                    double_arr.push_back(static_cast<double>(item.asInt()));
                } else if (item.isDouble()) {
                    double_arr.push_back(item.asDouble());
                } else {
                    throw std::runtime_error("Mixed types in number array");
                }
            }
            return EventValue::from_double_vector(double_arr);
        }
        else {
            // 不支持的类型，返回空字符串数组
            return EventValue::from_string_vector({});
        }
    }
    else if (json.isObject()) {
        // 对象转换为JSON字符串
        return EventValue::from_string(json.toString());
    }
    else if (json.isNull()) {
        // 对于null，返回空字符串
        return EventValue::from_string("");
    }
    else {
        throw std::runtime_error("Unsupported JSON type for EventValue");
    }
}

EventValue EventValue::from_string(const std::string& str) {
    return EventValue(str);
}

EventValue EventValue::from_int(int64_t value) {
    return EventValue(value);
}

EventValue EventValue::from_double(double value) {
    return EventValue(value);
}

EventValue EventValue::from_bool(bool value) {
    return EventValue(value);
}

EventValue EventValue::from_string_vector(const std::vector<std::string>& vec) {
    return EventValue(vec);
}

EventValue EventValue::from_double_vector(const std::vector<double>& vec) {
    return EventValue(vec);
}