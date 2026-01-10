// core/exception.cpp
#include "foundation/core/exception.hpp"

namespace foundation {

Exception::Exception(const std::string& message, const std::string& file, int line)
    : message_(message), file_(file), line_(line) {}

const char* Exception::what() const noexcept {
    return message_.c_str();
}

std::string Exception::fullMessage() const {
    std::string fullMsg;
    
    if (!file_.empty()) {
        fullMsg += "[" + file_;
        if (line_ > 0) {
            fullMsg += ":" + std::to_string(line_);
        }
        fullMsg += "] ";
    }
    
    fullMsg += message_;
    return fullMsg;
}

std::string Exception::file() const {
    return file_;
}

int Exception::line() const {
    return line_;
}

} // namespace foundation