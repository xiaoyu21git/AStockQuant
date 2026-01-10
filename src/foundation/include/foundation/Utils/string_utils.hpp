#pragma once
#ifndef FOUNDATION_UTILS_STRING_UTILS_HPP
#define FOUNDATION_UTILS_STRING_UTILS_HPP

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

namespace foundation {
namespace utils {

class StringUtils {
public:
    // 字符串分割
    static std::vector<std::string> split(const std::string& str, char delimiter);
    static std::vector<std::string> split(const std::string& str, const std::string& delimiters);
    
    // 字符串修剪
    static std::string trim(const std::string& str);
    static std::string ltrim(const std::string& str);
    static std::string rtrim(const std::string& str);
    
    // 大小写转换
    static std::string toLower(const std::string& str);
    static std::string toUpper(const std::string& str);
    
    // 检查前缀和后缀
    static bool startsWith(const std::string& str, const std::string& prefix);
    static bool endsWith(const std::string& str, const std::string& suffix);
    
    // 字符串替换
    static std::string replace(const std::string& str, 
                              const std::string& from, 
                              const std::string& to);
    
    // 字符串连接
    static std::string join(const std::vector<std::string>& strings, 
                           const std::string& delimiter);
    
    // 检查是否包含子串
    static bool contains(const std::string& str, const std::string& substr);
    
    // 重复字符串
    static std::string repeat(const std::string& str, int times);
    
    // 移除字符
    static std::string remove(const std::string& str, char ch);
    
    // 格式化字符串
    template<typename... Args>
    static std::string format(const std::string& format, Args... args);
};

} // namespace utils
} // namespace foundation

#endif // FOUNDATION_UTILS_STRING_UTILS_HPP