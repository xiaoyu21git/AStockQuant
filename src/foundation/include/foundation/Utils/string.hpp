// utils/string.hpp - 字符串工具接口
#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace foundation {
namespace utils {

class String {
public:
    // 去除空白字符
    static std::string trim(const std::string& str);
    static std::string trimLeft(const std::string& str);
    static std::string trimRight(const std::string& str);
    
    // 大小写转换
    static std::string toLower(const std::string& str);
    static std::string toUpper(const std::string& str);
    static std::string capitalize(const std::string& str);
    static std::string uncapitalize(const std::string& str);
    
    // 字符串检查
    static bool startsWith(const std::string& str, const std::string& prefix);
    static bool endsWith(const std::string& str, const std::string& suffix);
    static bool contains(const std::string& str, const std::string& substr);
    static bool contains(const std::string& str, char ch);
    
    // 字符串分割与连接
    static std::vector<std::string> split(const std::string& str, char delimiter);
    static std::vector<std::string> split(const std::string& str, const std::string& delimiter);
    static std::string join(const std::vector<std::string>& strings, const std::string& delimiter);
    static std::string join(const std::vector<std::string>& strings, char delimiter);
    
    // 字符串替换
    static std::string replace(const std::string& str, const std::string& from, 
                               const std::string& to);
    static std::string replaceAll(const std::string& str, const std::string& from, 
                                  const std::string& to);
    
    // 编码解码
    static std::string urlEncode(const std::string& str);
    static std::string urlDecode(const std::string& str);
    static std::string base64Encode(const std::string& str);
    static std::string base64Decode(const std::string& str);
    static std::string htmlEscape(const std::string& str);
    static std::string htmlUnescape(const std::string& str);
    
    // 哈希计算
    static std::string md5(const std::string& str);
    static std::string sha1(const std::string& str);
    static std::string sha256(const std::string& str);
    
    // 格式化
    static std::string format(const std::string& fmt, ...);
    static std::string formatv(const std::string& fmt, va_list args);
    
    // 随机字符串
    static std::string random(size_t length, const std::string& charset = "");
    
    // 类型转换
    template<typename T>
    static T toNumber(const std::string& str);
    
    template<typename T>
    static std::string toString(const T& value);
    
    // 正则表达式（简化版）
    static bool matches(const std::string& str, const std::string& pattern);
    static std::string extract(const std::string& str, const std::string& pattern);
    static std::vector<std::string> extractAll(const std::string& str, const std::string& pattern);
};

} // namespace utils
} // namespace foundation