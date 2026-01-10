// core/exception.hpp
#pragma once

#include <string>
#include <exception>

namespace foundation {

class Exception : public std::exception {
private:
    std::string message_;
    std::string file_;
    int line_;

public:
    Exception(const std::string& message, const std::string& file = "", int line = 0);
    virtual ~Exception() = default;
    
    const char* what() const noexcept override;
    std::string fullMessage() const;
    std::string file() const;
    int line() const;
};

class RuntimeException : public Exception {
public:
    using Exception::Exception;
};

class FileException : public Exception {
public:
    using Exception::Exception;
};

class NetworkException : public Exception {
public:
    using Exception::Exception;
};

class ParseException : public Exception {
public:
    using Exception::Exception;
};

class ConfigException : public Exception {
public:
    using Exception::Exception;
};

} // namespace foundation