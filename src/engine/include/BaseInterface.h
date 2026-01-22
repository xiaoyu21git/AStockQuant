#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <functional>
namespace engine {


struct Error {
    std::string message;
    int code = 0;
    
    Error() = default;
    Error(int c, std::string m) : code(c), message(std::move(m)) {}
    
    // 方法1：使用 ok() 方法
    bool ok() const { return code == 0; }
    
    // 方法2：使用 is_error() 方法
    bool is_error() const { return code != 0; }
    
    // 方法3：使用 operator bool() 表示是否有错误
    explicit operator bool() const { return code != 0; }
    
    static Error success() { return Error(); }
    static Error fail(int c, std::string m) { return Error(c, std::move(m)); }
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