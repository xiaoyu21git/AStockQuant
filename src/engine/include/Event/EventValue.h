#pragma once

#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <type_traits>
#include "foundation/json/json_facade.h"

class EventValue {
public:
    // 支持的类型
    using ValueType = std::variant<
        std::string,
        int64_t,
        double,
        bool,
        std::vector<std::string>,
        std::vector<double>
    >;

private:
    ValueType value_;

public:
    // ===== 构造函数 =====
    EventValue() = default;
    
    // 模板构造函数，支持所有变体类型
    template<typename T,
             typename = std::enable_if_t<std::is_constructible_v<ValueType, T>>>
    explicit EventValue(T&& value) : value_(std::forward<T>(value)) {}
    
    // ===== 静态工厂方法（推荐） =====
    
    // 从 JSON 创建
    static EventValue from_json(const foundation::json::JsonFacade& json);
    
    // 从各种类型创建
    static EventValue from_string(const std::string& str);
    static EventValue from_int(int64_t value);
    static EventValue from_double(double value);
    static EventValue from_bool(bool value);
    static EventValue from_string_vector(const std::vector<std::string>& vec);
    static EventValue from_double_vector(const std::vector<double>& vec);
    
    // ===== 类型检查 =====
    bool is_string() const { return std::holds_alternative<std::string>(value_); }
    bool is_int64() const { return std::holds_alternative<int64_t>(value_); }
    bool is_double() const { return std::holds_alternative<double>(value_); }
    bool is_bool() const { return std::holds_alternative<bool>(value_); }
    bool is_string_vector() const { return std::holds_alternative<std::vector<std::string>>(value_); }
    bool is_double_vector() const { return std::holds_alternative<std::vector<double>>(value_); }
    
    // 获取类型名称
    std::string type_name() const {
        if (is_string()) return "string";
        if (is_int64()) return "int64";
        if (is_double()) return "double";
        if (is_bool()) return "bool";
        if (is_string_vector()) return "string_vector";
        if (is_double_vector()) return "double_vector";
        return "unknown";
    }
    
    // ===== 值获取（类型安全） =====
    
    // 模板方法获取值
    template<typename T>
    std::optional<T> get() const {
        if (const T* ptr = std::get_if<T>(&value_)) {
            return *ptr;
        }
        return std::nullopt;
    }
    
    // 便捷方法
    std::optional<std::string> get_string() const { return get<std::string>(); }
    std::optional<int64_t> get_int64() const { return get<int64_t>(); }
    std::optional<double> get_double() const { return get<double>(); }
    std::optional<bool> get_bool() const { return get<bool>(); }
    std::optional<std::vector<std::string>> get_string_vector() const { 
        return get<std::vector<std::string>>(); 
    }
    std::optional<std::vector<double>> get_double_vector() const { 
        return get<std::vector<double>>(); 
    }
    
    // 带默认值的获取方法
    template<typename T>
    T get_or_default(const T& default_value) const {
        return get<T>().value_or(default_value);
    }
    
    // ===== 转换方法 =====
    
    // 转换为字符串
    std::string to_string() const {
        return std::visit([](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            
            if constexpr (std::is_same_v<T, std::string>) {
                return arg;
            }
            else if constexpr (std::is_same_v<T, int64_t>) {
                return std::to_string(arg);
            }
            else if constexpr (std::is_same_v<T, double>) {
                // 控制浮点数输出格式
                std::string str = std::to_string(arg);
                // 移除多余的0
                str.erase(str.find_last_not_of('0') + 1, std::string::npos);
                if (str.back() == '.') {
                    str.pop_back();
                }
                return str;
            }
            else if constexpr (std::is_same_v<T, bool>) {
                return arg ? "true" : "false";
            }
            else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                std::string result = "[";
                for (size_t i = 0; i < arg.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += "\"" + arg[i] + "\"";
                }
                result += "]";
                return result;
            }
            else if constexpr (std::is_same_v<T, std::vector<double>>) {
                std::string result = "[";
                for (size_t i = 0; i < arg.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += std::to_string(arg[i]);
                }
                result += "]";
                return result;
            }
            else {
                return "unknown";
            }
        }, value_);
    }
    
    // 转换为 JSON
    foundation::json::JsonFacade to_json() const {
        return std::visit([](auto&& arg) -> foundation::json::JsonFacade {
            using T = std::decay_t<decltype(arg)>;
            
            if constexpr (std::is_same_v<T, std::string>) {
                return foundation::json::JsonFacade::createString(arg);
            }
            else if constexpr (std::is_same_v<T, int64_t>) {
                return foundation::json::JsonFacade::createInt(static_cast<int>(arg));
            }
            else if constexpr (std::is_same_v<T, double>) {
                return foundation::json::JsonFacade::createDouble(arg);
            }
            else if constexpr (std::is_same_v<T, bool>) {
                return foundation::json::JsonFacade::createBool(arg);
            }
            else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                auto arr = foundation::json::JsonFacade::createArray();
                for (const auto& item : arg) {
                    arr.push_back(foundation::json::JsonFacade::createString(item));
                }
                return arr;
            }
            else if constexpr (std::is_same_v<T, std::vector<double>>) {
                auto arr = foundation::json::JsonFacade::createArray();
                for (const auto& item : arg) {
                    arr.push_back(foundation::json::JsonFacade::createDouble(item));
                }
                return arr;
            }
            else {
                return foundation::json::JsonFacade::createNull();
            }
        }, value_);
    }
    
    // ===== 底层访问 =====
    const ValueType& value() const { return value_; }
    ValueType& value() { return value_; }
    
    // ===== 运算符重载 =====
    bool operator==(const EventValue& other) const {
        return value_ == other.value_;
    }
    
    bool operator!=(const EventValue& other) const {
        return !(*this == other);
    }
    
    // ===== 辅助方法 =====
    bool is_null() const {
        return value_.index() == 0 && std::get<0>(value_).empty();
    }
    
    // 尝试转换为指定类型（可能抛出异常）
    template<typename T>
    T as() const {
        return std::get<T>(value_);
    }
    
    // 安全转换，失败返回默认值
    template<typename T>
    T as_or_default(const T& default_value) const {
        try {
            return std::get<T>(value_);
        } catch (const std::bad_variant_access&) {
            return default_value;
        }
    }
};