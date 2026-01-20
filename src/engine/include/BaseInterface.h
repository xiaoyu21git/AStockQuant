#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <functional>

namespace engine {

// 基础类型别名
using Timestamp = std::chrono::system_clock::time_point;
using Duration = std::chrono::nanoseconds;

// 错误类型
struct Error {
    std::string message;
    int code = 0;

    Error() = default; // 默认构造函数
    Error(int c, std::string m) : code(c), message(std::move(m)) {} // 明确构造函数

    static Error ok() { return Error(); }
    static Error fail(int c, std::string m) { return Error(c, std::move(m)); }

    explicit operator bool() const { return code != 0; }
};

// 结果包装器
template<typename T>
class Result {
public:
    // 成功构造
    Result(T&& value) : value_(std::move(value)), error_{} {}
    
    // 错误构造
    Result(Error error) : error_(error) {}
    
    bool success() const { return error_.code == 0; }
    bool failed() const { return error_.code != 0; }
    
    const T& value() const { return value_; }
    T&& take_value() { return std::move(value_); }
    
    const Error& error() const { return error_; }
    
private:
    T value_;
    Error error_;
};

} // namespace engine