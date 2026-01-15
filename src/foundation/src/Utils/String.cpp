// utils/string.cpp
#include "foundation/utils/string.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <random>
#include <cstdarg> 
// #include <openssl/md5.h>
// #include <openssl/sha.h>

// 如果不需要 OpenSSL，可以注释掉并实现自己的哈希函数
// #define USE_OPENSSL

namespace foundation {
namespace utils {
// ============ 初始化线程局部缓存 ============
thread_local std::vector<String::SplitCacheEntry> String::t_split_cache_;
// ============ 去除空白字符 ============
std::string String::trim(const std::string& str) {
    return trimRight(trimLeft(str));
}

std::string String::trimLeft(const std::string& str) {
    auto start = std::find_if_not(str.begin(), str.end(), 
        [](unsigned char ch) { return std::isspace(ch); });
    return std::string(start, str.end());
}

std::string String::trimRight(const std::string& str) {
    auto end = std::find_if_not(str.rbegin(), str.rend(),
        [](unsigned char ch) { return std::isspace(ch); }).base();
    return std::string(str.begin(), end);
}

// ============ 大小写转换 ============
std::string String::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string String::toUpper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return std::toupper(c); });
    return result;
}

std::string String::capitalize(const std::string& str) {
    if (str.empty()) return "";
    std::string result = str;
    result[0] = static_cast<char>(std::toupper(result[0]));
    return result;
}

std::string String::uncapitalize(const std::string& str) {
    if (str.empty()) return "";
    std::string result = str;
    result[0] = static_cast<char>(std::tolower(result[0]));
    return result;
}

// ============ 字符串检查 ============
bool String::startsWith(const std::string& str, const std::string& prefix) {
    if (prefix.size() > str.size()) return false;
    return std::equal(prefix.begin(), prefix.end(), str.begin());
}

bool String::endsWith(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
}

bool String::contains(const std::string& str, const std::string& substr) {
    return str.find(substr) != std::string::npos;
}

bool String::contains(const std::string& str, char ch) {
    return str.find(ch) != std::string::npos;
}

// ============ 字符串分割与连接 ============
std::vector<std::string> String::split(const std::string& str, char delimiter) {
     // 1. 尝试从缓存获取
    std::vector<std::string> cached_result;
    if (getFromSplitCache(str, delimiter, cached_result)) {
        return cached_result;
    }
    
    // 2. 执行分割（使用优化后的实现）
    std::vector<std::string> result;
    
    if (str.empty()) {
        updateSplitCache(str, delimiter, result);
        return result;
    }
    
    // 预分配内存
    size_t estimated_parts = 1;
    for (char c : str) {
        if (c == delimiter) estimated_parts++;
    }
    result.reserve(std::min(estimated_parts, size_t(10)));
    
    // 使用优化的分割逻辑
    size_t start = 0;
    size_t end = str.find(delimiter);
    
    while (end != std::string::npos) {
        if (end != start) {  // 避免空字符串
            result.push_back(str.substr(start, end - start));
        }
        start = end + 1;
        end = str.find(delimiter, start);
    }
    
    // 添加最后一部分
    if (start < str.length()) {
        result.push_back(str.substr(start));
    }
    
    // 3. 更新缓存
    updateSplitCache(str, delimiter, result);
    
    return result;
}

std::vector<std::string> String::split(const std::string& str, const std::string& delimiter) {
       // 对于多字符分隔符，无法使用相同优化，但可以预分配内存
    std::vector<std::string> result;
    
    if (str.empty() || delimiter.empty()) {
        result.push_back(str);
        return result;
    }
    
    // 预分配内存（估计）
    size_t estimated_parts = 1;
    size_t pos = 0;
    while ((pos = str.find(delimiter, pos)) != std::string::npos) {
        estimated_parts++;
        pos += delimiter.length();
    }
    
    result.reserve(std::min(estimated_parts, size_t(10)));
    
    size_t start = 0;
    size_t end = str.find(delimiter);
    
    while (end != std::string::npos) {
        result.push_back(str.substr(start, end - start));
        start = end + delimiter.length();
        end = str.find(delimiter, start);
    }
    
    result.push_back(str.substr(start));
    return result;
}
// ============ 实现 splitInto 方法 ============
void String::splitInto(std::string_view str, char delimiter,
                      std::vector<std::string_view>& result) {
    result.clear();
    
    if (str.empty()) {
        return;
    }
    
    // 使用指针操作，极致性能
    const char* begin = str.data();
    const char* end = begin + str.size();
    
    // 预估计段数，避免多次扩容
    size_t estimated_parts = 1;
    for (const char* p = begin; p < end; ++p) {
        if (*p == delimiter) estimated_parts++;
    }
    result.reserve(std::min(estimated_parts, size_t(10)));
    
    // 核心分割逻辑
    const char* current = begin;
    const char* segment_start = begin;
    
    while (current < end) {
        if (*current == delimiter) {
            // 只有当段不为空时才添加
            if (current > segment_start) {
                result.emplace_back(segment_start, current - segment_start);
            }
            segment_start = current + 1;
        }
        current++;
    }
    
    // 处理最后一段
    if (segment_start < end) {
        result.emplace_back(segment_start, end - segment_start);
    }
}
// ============ 实现缓存管理方法 ============
bool String::getFromSplitCache(const std::string& str, char delimiter,
                              std::vector<std::string>& result) {
    auto now = std::chrono::steady_clock::now();
    
    for (auto& entry : t_split_cache_) {
        if (entry.matches(str, delimiter)) {
            entry.last_access = now;
            entry.hit_count++;
            result = entry.parts;
            return true;
        }
    }
    
    return false;
}
void String::updateSplitCache(const std::string& str, char delimiter,
                             const std::vector<std::string>& parts) {
    auto now = std::chrono::steady_clock::now();
    
    // 检查是否已存在
    for (auto& entry : t_split_cache_) {
        if (entry.matches(str, delimiter)) {
            entry.parts = parts;
            entry.last_access = now;
            entry.hit_count++;
            return;
        }
    }
    
    // 缓存满时，移除访问最少的
    if (t_split_cache_.size() >= MAX_SPLIT_CACHE_SIZE) {
        auto min_it = std::min_element(
            t_split_cache_.begin(), t_split_cache_.end(),
            [](const SplitCacheEntry& a, const SplitCacheEntry& b) {
                return a.hit_count < b.hit_count;
            }
        );
        
        if (min_it != t_split_cache_.end()) {
            *min_it = {str, delimiter, parts, 1, now};
            return;
        }
    }
    
    // 添加新条目
    t_split_cache_.push_back({str, delimiter, parts, 1, now});
}
// ============ 实现 splitCached 方法 ============
std::vector<std::string> String::splitCached(const std::string& str, char delimiter) {
    // 直接使用优化后的 split（已经包含缓存）
    return split(str, delimiter);
}
// ============ 实现 clearSplitCache 方法 ============
void String::clearSplitCache() {
    t_split_cache_.clear();
}
// ============ 实现路径专用分割方法 ============
std::vector<std::string> String::splitPath(const std::string& path) {
    // 路径分割通常使用 '.' 作为分隔符
    return splitCached(path, '.');
}

std::string String::join(const std::vector<std::string>& strings, const std::string& delimiter) {
    if (strings.empty()) return "";
    if (strings.size() == 1) return strings[0];
    
    // 预计算总长度
    size_t total_length = 0;
    for (const auto& s : strings) {
        total_length += s.length();
    }
    total_length += delimiter.length() * (strings.size() - 1);
    
    // 预分配内存
    std::string result;
    result.reserve(total_length);
    
    // 拼接
    result = strings[0];
    for (size_t i = 1; i < strings.size(); ++i) {
        result += delimiter;
        result += strings[i];
    }
    
    return result;
}

std::string String::join(const std::vector<std::string>& strings, char delimiter) {
    return join(strings, std::string(1, delimiter));
}

// ============ 字符串替换 ============
std::string String::replace(const std::string& str, const std::string& from, 
                           const std::string& to) {
    size_t start_pos = str.find(from);
    if (start_pos == std::string::npos) return str;
    
    std::string result = str;
    result.replace(start_pos, from.length(), to);
    return result;
}

std::string String::replaceAll(const std::string& str, const std::string& from, 
                              const std::string& to) {
    if (from.empty()) return str;
    
    std::string result = str;
    size_t start_pos = 0;
    
    while ((start_pos = result.find(from, start_pos)) != std::string::npos) {
        result.replace(start_pos, from.length(), to);
        start_pos += to.length(); // 避免无限循环
    }
    
    return result;
}

// ============ URL 编码解码 ============
std::string String::urlEncode(const std::string& str) {
    static const char hexChars[] = "0123456789ABCDEF";
    std::string result;
    
    for (unsigned char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += c;
        } else {
            result += '%';
            result += hexChars[(c >> 4) & 0x0F];
            result += hexChars[c & 0x0F];
        }
    }
    
    return result;
}

std::string String::urlDecode(const std::string& str) {
    std::string result;
    
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            std::string hex = str.substr(i + 1, 2);
            char c = static_cast<char>(std::stoi(hex, nullptr, 16));
            result += c;
            i += 2;
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    
    return result;
}

// ============ Base64 编码解码 ============
static const std::string BASE64_CHARS = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string String::base64Encode(const std::string& str) {
    std::string result;
    int val = 0;
    int bits = -6;
    const unsigned int b63 = 0x3F;
    
    for (unsigned char c : str) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) {
            result.push_back(BASE64_CHARS[(val >> bits) & b63]);
            bits -= 6;
        }
    }
    
    if (bits > -6) {
        result.push_back(BASE64_CHARS[((val << 8) >> (bits + 8)) & b63]);
    }
    
    while (result.size() % 4) {
        result.push_back('=');
    }
    
    return result;
}

std::string String::base64Decode(const std::string& str) {
    std::string result;
    std::vector<int> T(256, -1);
    
    for (int i = 0; i < 64; i++) {
        T[BASE64_CHARS[i]] = i;
    }
    
    int val = 0, bits = -8;
    for (unsigned char c : str) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        bits += 6;
        
        if (bits >= 0) {
            result.push_back(char((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    
    return result;
}

// ============ HTML 转义 ============
std::string String::htmlEscape(const std::string& str) {
    std::string result;
    result.reserve(str.length() * 2);
    
    for (char c : str) {
        switch (c) {
            case '&':  result.append("&amp;");  break;
            case '\"': result.append("&quot;"); break;
            case '\'': result.append("&apos;"); break;
            case '<':  result.append("&lt;");   break;
            case '>':  result.append("&gt;");   break;
            default:   result.push_back(c);     break;
        }
    }
    
    return result;
}

std::string String::htmlUnescape(const std::string& str) {
    std::string result = str;
    
    result = replaceAll(result, "&amp;",  "&");
    result = replaceAll(result, "&quot;", "\"");
    result = replaceAll(result, "&apos;", "'");
    result = replaceAll(result, "&lt;",   "<");
    result = replaceAll(result, "&gt;",   ">");
    
    return result;
}

// ============ 哈希计算 ============
#ifdef USE_OPENSSL
std::string String::md5(const std::string& str) {
    MD5_CTX context;
    MD5_Init(&context);
    MD5_Update(&context, str.c_str(), str.length());
    
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_Final(digest, &context);
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char c : digest) {
        oss << std::setw(2) << static_cast<int>(c);
    }
    
    return oss.str();
}

std::string String::sha1(const std::string& str) {
    SHA_CTX context;
    SHA1_Init(&context);
    SHA1_Update(&context, str.c_str(), str.length());
    
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1_Final(digest, &context);
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char c : digest) {
        oss << std::setw(2) << static_cast<int>(c);
    }
    
    return oss.str();
}

std::string String::sha256(const std::string& str) {
    SHA256_CTX context;
    SHA256_Init(&context);
    SHA256_Update(&context, str.c_str(), str.length());
    
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_Final(digest, &context);
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char c : digest) {
        oss << std::setw(2) << static_cast<int>(c);
    }
    
    return oss.str();
}
#else
// 简单的哈希函数实现（不用于安全目的）
std::string String::md5(const std::string& str) {
    // 简单实现，返回固定长度的伪哈希
    const std::string charset = "0123456789abcdef";
    std::mt19937 rng(std::hash<std::string>{}(str));
    std::uniform_int_distribution<> dist(0, charset.size() - 1);
    
    std::string result;
    for (int i = 0; i < 32; ++i) {
        result += charset[dist(rng)];
    }
    return result;
}

std::string String::sha1(const std::string& str) {
    // 简单实现
    return md5(str).substr(0, 40);
}

std::string String::sha256(const std::string& str) {
    // 简单实现
    return md5(str) + md5(str + "salt");
}
#endif

// ============ 格式化字符串 ============
std::string String::format(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::string result = formatv(fmt, args);
    va_end(args);
    return result;
}

std::string String::formatv(const char* fmt, va_list argss) {
      if (fmt == nullptr) {
        return "";
    }
    
    // 复制参数列表
    va_list args_copy;
    va_copy(args_copy, argss);
    
    // 第一次调用获取需要的缓冲区大小
    int length = std::vsnprintf(nullptr, 0, fmt, args_copy);
    va_end(args_copy);
    
    if (length < 0) {
        return "";
    }
    
    // 分配缓冲区
    std::vector<char> buffer(static_cast<size_t>(length) + 1);
    
    // 实际格式化
    int result = std::vsnprintf(buffer.data(), buffer.size(), fmt, argss);
    
    if (result < 0) {
        return "";
    }
    
    return std::string(buffer.data(), static_cast<size_t>(result));
}

// ============ 随机字符串 ============
std::string String::random(size_t length, const std::string& charset) {
    static const std::string DEFAULT_CHARSET = 
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    
    const std::string& chars = charset.empty() ? DEFAULT_CHARSET : charset;
    if (chars.empty() || length == 0) {
        return "";
    }
    
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<size_t> dist(0, chars.size() - 1);
    
    std::string result;
    result.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        result += chars[dist(rng)];
    }
    
    return result;
}

// ============ 类型转换模板特化 ============
template<>
int String::toNumber<int>(const std::string& str) {
    try {
        return std::stoi(str);
    } catch (...) {
        return 0;
    }
}

template<>
long String::toNumber<long>(const std::string& str) {
    try {
        return std::stol(str);
    } catch (...) {
        return 0L;
    }
}

template<>
long long String::toNumber<long long>(const std::string& str) {
    try {
        return std::stoll(str);
    } catch (...) {
        return 0LL;
    }
}

template<>
float String::toNumber<float>(const std::string& str) {
    try {
        return std::stof(str);
    } catch (...) {
        return 0.0f;
    }
}

template<>
double String::toNumber<double>(const std::string& str) {
    try {
        return std::stod(str);
    } catch (...) {
        return 0.0;
    }
}

template<>
std::string String::toString<int>(const int& value) {
    return std::to_string(value);
}

template<>
std::string String::toString<long>(const long& value) {
    return std::to_string(value);
}

template<>
std::string String::toString<double>(const double& value) {
    return std::to_string(value);
}

template<>
std::string String::toString<bool>(const bool& value) {
    return value ? "true" : "false";
}

// ============ 正则表达式（简化版） ============
bool String::matches(const std::string& str, const std::string& pattern) {
    // 简化实现：只支持简单的通配符
    if (pattern.empty()) return str.empty();
    
    size_t strPos = 0, patPos = 0;
    size_t strLen = str.length(), patLen = pattern.length();
    
    while (patPos < patLen && strPos < strLen) {
        if (pattern[patPos] == '*') {
            // 跳过连续的 *
            while (patPos < patLen && pattern[patPos] == '*') {
                ++patPos;
            }
            
            if (patPos == patLen) {
                return true; // 模式以 * 结尾，匹配所有剩余字符
            }
            
            // 在字符串中查找匹配下一个模式字符的位置
            while (strPos < strLen && str[strPos] != pattern[patPos]) {
                ++strPos;
            }
            
            if (strPos == strLen) {
                return false; // 找不到匹配
            }
        } else if (pattern[patPos] == '?' || pattern[patPos] == str[strPos]) {
            ++patPos;
            ++strPos;
        } else {
            return false;
        }
    }
    
    // 处理剩余的模式字符（只允许是 *）
    while (patPos < patLen && pattern[patPos] == '*') {
        ++patPos;
    }
    
    return patPos == patLen && strPos == strLen;
}

std::string String::extract(const std::string& str, const std::string& pattern) {
    // 简化实现：返回第一个匹配的子串
    if (pattern.empty()) return "";
    
    size_t pos = str.find(pattern);
    if (pos != std::string::npos) {
        return str.substr(pos, pattern.length());
    }
    
    return "";
}

std::vector<std::string> String::extractAll(const std::string& str, const std::string& pattern) {
    std::vector<std::string> result;
    if (pattern.empty()) return result;
    
    size_t pos = 0;
    while ((pos = str.find(pattern, pos)) != std::string::npos) {
        result.push_back(str.substr(pos, pattern.length()));
        pos += pattern.length();
    }
    
    return result;
}

} // namespace utils
} // namespace foundation