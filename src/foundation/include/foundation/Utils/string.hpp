// utils/string.hpp - 字符串工具接口
#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>  // 添加 string_view
#include <chrono>       // 添加 chrono 用于缓存
#include <shared_mutex> // 添加共享锁
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
    static std::string format(const char* fmt, ...);
    
    static std::string formatv(const char* fmt, va_list args);
    
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
    // ============ 新增优化方法 ============
    
    /**
     * @brief 高性能分割，直接填充到已有vector，避免重复分配
     * @param str 要分割的字符串（使用string_view避免拷贝）
     * @param delimiter 分隔符
     * @param result 结果存储的vector（会被清空后填充）
     */
    static void splitInto(std::string_view str, char delimiter,
                         std::vector<std::string_view>& result);
    
    /**
     * @brief 带缓存的split版本，适用于重复分割相同字符串
     */
    static std::vector<std::string> splitCached(const std::string& str, char delimiter);
    
    /**
     * @brief 清空分割缓存（主要用于测试）
     */
    static void clearSplitCache();
    
    // ============ 新增路径专用方法 ============
    
    /**
     * @brief 路径专用分割（针对'.'分隔符优化）
     */
    static std::vector<std::string> splitPath(const std::string& path);
    
    /**
     * @brief 路径专用分割，使用string_view
     */
    static void splitPathInto(std::string_view path, 
                             std::vector<std::string_view>& result);
private:
 // ============ 分割缓存结构 ============
    struct SplitCacheEntry {
        std::string input;
        char delimiter;
        std::vector<std::string> parts;
        size_t hit_count = 0;
        std::chrono::steady_clock::time_point last_access;
        
        bool matches(const std::string& in, char delim) const {
            return input == in && delimiter == delim;
        }
    };
    
    // 线程局部分割缓存
    static thread_local std::vector<SplitCacheEntry> t_split_cache_;
    static const size_t MAX_SPLIT_CACHE_SIZE = 200;
    
    // 缓存管理方法
    static bool getFromSplitCache(const std::string& str, char delimiter,
                                 std::vector<std::string>& result);
    static void updateSplitCache(const std::string& str, char delimiter,
                                const std::vector<std::string>& parts);
    
    // 优化的核心分割实现
    static void splitInternal(const char* begin, const char* end, char delimiter,
                             std::vector<std::string_view>& result);
};

} // namespace utils
} // namespace foundation