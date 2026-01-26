#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <functional>
namespace engine {

class Error {
public:
    enum Code {
        OK = 0,
        INVALID_ARGUMENT, 
        NOT_FOUND,
        ALREADY_EXISTS,
        BUS_STOPPED,
        TIMEOUT,
        RESOURCE_EXHAUSTED,
        BUSY,
        CONNECTED,
        DISCONNECTED
    };
    
    Error(Code code = OK, const std::string& msg = "") 
        : code_(code), message_(msg) {}
    
    bool ok() const { return code_ == OK; }
    Code code() const { return code_; }
    const std::string& message() const { return message_; }
    static Error success() { return Error(); }
    static Error fail(Code code, std::string m) { return Error(code, std::move(m)); }
private:
    Code code_;
    std::string message_;
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