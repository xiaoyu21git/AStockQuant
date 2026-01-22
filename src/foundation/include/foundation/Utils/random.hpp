// utils/random.hpp - 随机数工具头文件
#pragma once

#include <random>
#include <string>
#include <vector>
#include <cstdint>
#include <type_traits>
#include <algorithm>
#include <mutex>
#include <chrono>

namespace foundation {
namespace utils {

class Random {
private:
    static std::mt19937& getGenerator() {
        static thread_local std::random_device rd;
        static thread_local std::mt19937 generator(rd());
        return generator;
    }
    
public:
    // ============ 基本随机数生成 ============
     // 添加静态种子设置方法
    static void setGlobalSeed(unsigned int seed) {
        // 重置随机数生成器的种子
        getGenerator().seed(seed);
    }
   // 生成随机整数 [min, max]
    template<typename T>
    static T getInt(T min, T max) {
        // 总是使用 long long 作为分布类型，然后转换
        static_assert(std::is_integral_v<T>, "T must be an integral type");
    
        std::uniform_int_distribution<long long> dist(min, max);
        return static_cast<T>(dist(getGenerator()));
    }
    
    // 生成随机浮点数 [min, max)
    template<typename T, typename = std::enable_if_t<std::is_floating_point_v<T>>>
    static T getFloat(T min, T max) {
        std::uniform_real_distribution<T> dist(min, max);
        return dist(getGenerator());
    }
    
    // 生成随机布尔值
    static bool getBool(double probability = 0.5) {
        std::bernoulli_distribution dist(probability);
        return dist(getGenerator());
    }
    
    // ============ 随机字符串 ============
    
    // 生成随机字符串（默认使用字母数字）
    static std::string getString(size_t length);
    
    // 从指定字符集生成随机字符串
    static std::string getString(size_t length, const std::string& charset);
    
    // 生成随机十六进制字符串
    static std::string getHexString(size_t length, bool uppercase = false);
    
    // 生成随机Base64字符串
    static std::string getBase64String(size_t length);
    
    // ============ UUID 生成 ============
    
    // 生成UUID v4（标准格式）
    static std::string generateUuid();
    
    // 生成简化UUID（无连字符）
    static std::string generateSimpleUuid();
    
    // ============ 随机选择 ============
    
    // 从容器中随机选择一个元素
    template<typename Container>
    static typename Container::value_type choice(const Container& container) {
        if (container.empty()) {
            throw std::invalid_argument("Container is empty");
        }
        
        auto it = container.begin();
        std::advance(it, getInt<size_t>(0, container.size() - 1));
        return *it;
    }
    
    // 从列表中随机选择多个元素（不重复）
    template<typename Container>
    static std::vector<typename Container::value_type> sample(
        const Container& container, size_t count);
    
    // 从列表中随机选择多个元素（可能重复）
    template<typename Container>
    static std::vector<typename Container::value_type> choices(
        const Container& container, size_t count);
    
    // ============ 随机分布 ============
    
    // 正态分布
    template<typename T = double>
    static T getNormal(T mean = 0.0, T stddev = 1.0);
    
    // 指数分布
    template<typename T = double>
    static T getExponential(T lambda = 1.0);
    
    // 泊松分布
    static int getPoisson(double lambda = 1.0);
    
    // 二项分布
    static int getBinomial(int trials, double probability = 0.5);
    
    // 几何分布
    static int getGeometric(double probability = 0.5);
    
    // ============ 随机排列 ============
    
    // 随机打乱容器
    template<typename Container>
    static void shuffle(Container& container);
    
    // 生成随机排列
    template<typename T>
    static std::vector<T> permutation(size_t n);
    
    // ============ 种子管理 ============
    
    // 设置随机种子
    static void seed(unsigned int seed);
    
    // 使用时间戳作为种子
    static void seedWithTime();
    
    // 使用随机设备作为种子
    static void seedWithRandomDevice();
    
    // ============ 密码学安全随机数 ============
    
    // 生成密码学安全的随机字节
    static std::vector<uint8_t> getSecureRandomBytes(size_t count);
    
    // 生成密码学安全的随机整数
    template<typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    static T getSecureRandomInt(T min, T max);
    
    // ============ 随机工具 ============
    
    // 生成随机颜色（RGB）
    static std::tuple<uint8_t, uint8_t, uint8_t> getColor();
    
    // 生成随机颜色（RGBA）
    static std::tuple<uint8_t, uint8_t, uint8_t, uint8_t> getColorWithAlpha();
    
    // 生成随机颜色十六进制表示
    static std::string getColorHex(bool withAlpha , bool includeHash = true);
    
    // 生成随机IP地址
    static std::string getIpAddress();
    
    // 生成随机MAC地址
    static std::string getMacAddress();
    
    // 生成随机文件名
    static std::string getFilename(const std::string& extension = "");
    
    // ============ 概率相关 ============
    
    // 加权随机选择
    template<typename T>
    static T weightedChoice(const std::vector<T>& items, 
                           const std::vector<double>& weights);
    
    // 轮盘赌选择
    template<typename T>
    static T rouletteWheel(const std::vector<T>& items,
                          const std::vector<double>& weights);
    
    // ============ 随机数据生成 ============
    
    // 生成随机整数数组
    template<typename T = int>
    static std::vector<T> getIntArray(size_t count, T min, T max);
    
    // 生成随机浮点数数组
    template<typename T = double>
    static std::vector<T> getFloatArray(size_t count, T min, T max);
    
    // 生成随机字符串数组
    static std::vector<std::string> getStringArray(size_t count, size_t minLength, 
                                                  size_t maxLength);
        // ============ Next 系列函数 ============
    //辅助函数
    static std::string generateStringFromCharset(size_t length, const std::string& charset);
    static std::string generateUnicodeChar(uint32_t codePoint);
    static uint32_t randomCodePoint(const std::vector<uint32_t>& range);
    static std::string nextString(size_t length);
    
    /**
     * @brief 生成指定长度范围的随机字符串
     * @param minLength 最小长度
     * @param maxLength 最大长度
     * @return 随机字符串
     */
    static std::string nextString(size_t minLength, size_t maxLength);
    
    /**
     * @brief 从指定字符集生成随机字符串
     * @param length 字符串长度
     * @param charset 字符集
     * @return 随机字符串
     */
    static std::string nextString(size_t length, const std::string& charset);
    
    /**
     * @brief 生成随机字母字符串（仅包含字母）
     * @param length 字符串长度
     * @return 随机字母字符串
     */
    static std::string nextAlphaString(size_t length);
    
    /**
     * @brief 生成随机字母数字字符串
     * @param length 字符串长度
     * @return 随机字母数字字符串
     */
    static std::string nextAlnumString(size_t length);
    
    /**
     * @brief 生成随机数字字符串
     * @param length 字符串长度
     * @return 随机数字字符串
     */
    static std::string nextDigitString(size_t length);
    
    /**
     * @brief 生成随机十六进制字符串
     * @param length 字符串长度
     * @param uppercase 是否使用大写字母
     * @return 随机十六进制字符串
     */
    static std::string nextHexString(size_t length, bool uppercase = false);
    
    /**
     * @brief 生成随机Base64字符串
     * @param length 字符串长度
     * @return 随机Base64字符串
     */
    static std::string nextBase64String(size_t length);
    
    /**
     * @brief 生成随机可打印ASCII字符串
     * @param length 字符串长度
     * @return 随机可打印ASCII字符串
     */
    static std::string nextAsciiString(size_t length);
    
    /**
     * @brief 生成随机Unicode字符串（UTF-8）
     * @param length 字符串长度（字符数，不是字节数）
     * @return 随机Unicode字符串
     */
    static std::string nextUnicodeString(size_t length);
    /**
     * @brief 生成下一个随机整数
     * @return 随机整数
     * 
     * 默认返回 [0, std::numeric_limits<int>::max()) 范围内的随机整数
     */
    static int next_Int();
    
    /**
     * @brief 生成指定范围内的随机整数
     * @param bound 上界（不包含）
     * @return [0, bound) 范围内的随机整数
     */
    static int next_Int(int bound);
    
    /**
     * @brief 生成指定范围内的随机整数
     * @param min 下界（包含）
     * @param max 上界（包含）
     * @return [min, max] 范围内的随机整数
     */
    static int next_Int(int min, int max);
    
    /**
     * @brief 生成下一个随机长整数
     * @return 随机长整数
     */
    static long long next_Long();
    
    /**
     * @brief 生成指定范围内的随机长整数
     * @param bound 上界（不包含）
     * @return [0, bound) 范围内的随机长整数
     */
    static long long next_Long(long long bound);
    
    /**
     * @brief 生成指定范围内的随机长整数
     * @param min 下界（包含）
     * @param max 上界（包含）
     * @return [min, max] 范围内的随机长整数
     */
    static long long next_Long(long long min, long long max);
    
    /**
     * @brief 生成下一个随机双精度浮点数
     * @return [0.0, 1.0) 范围内的随机双精度浮点数
     */
    static double next_Double();
    
    /**
     * @brief 生成指定范围内的随机双精度浮点数
     * @param min 下界（包含）
     * @param max 上界（不包含）
     * @return [min, max) 范围内的随机双精度浮点数
     */
    static double next_Double(double min, double max);
    
    /**
     * @brief 生成下一个随机单精度浮点数
     * @return [0.0f, 1.0f) 范围内的随机单精度浮点数
     */
    static float next_Float();
    
    /**
     * @brief 生成指定范围内的随机单精度浮点数
     * @param min 下界（包含）
     * @param max 上界（不包含）
     * @return [min, max) 范围内的随机单精度浮点数
     */
    static float next_Float(float min, float max);
    
    /**
     * @brief 生成下一个随机布尔值
     * @return 随机布尔值
     */
    static bool next_Boolean();
    
    /**
     * @brief 生成指定概率的随机布尔值
     * @param probability true的概率，范围 [0.0, 1.0]
     * @return 随机布尔值
     */
    static bool next_Boolean(double probability);
    
    /**
     * @brief 生成下一个随机字节
     * @return 随机字节
     */
    static uint8_t next_Byte();
    
    /**
     * @brief 生成指定长度的随机字节数组
     * @param length 字节数组长度
     * @return 随机字节数组
     */
    static std::vector<uint8_t> next_Bytes(size_t length);
    // ============ 测试数据生成 ============
    
    // 生成随机姓名
    static std::string getName();
    
    // 生成随机邮箱
    static std::string getEmail();
    
    // 生成随机手机号
    static std::string getPhoneNumber();
    
    // 生成随机地址
    static std::string getAddress();
    
    // ============ 随机日期时间 ============
    
    // 生成随机日期
    static std::string getDate(int startYear = 2000, int endYear = 2024);
    
    // 生成随机时间
    static std::string getTime();
    
    // 生成随机日期时间
    static std::string getDateTime(int startYear = 2000, int endYear = 2024);
    
    // ============ 随机文件生成 ============
    
    // 生成随机文本文件内容
    static std::string getTextContent(size_t minWords = 10, size_t maxWords = 100);
    
    // 生成随机JSON数据
    static std::string getJsonData(size_t minItems = 3, size_t maxItems = 10);
    
    // 生成随机CSV数据
    static std::string getCsvData(size_t rows = 10, size_t cols = 5);
    
private:
 
      // 字符集定义
    static inline const std::string ALPHANUMERIC = 
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static inline const std::string ALPHABET = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static inline const std::string ALPHABET_UPPER = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static inline const std::string ALPHABET_LOWER = "abcdefghijklmnopqrstuvwxyz";
    static inline const std::string DIGITS = "0123456789";
    static inline const std::string HEX_LOWER = "0123456789abcdef";
    static inline const std::string HEX_UPPER = "0123456789ABCDEF";
    static inline const std::string BASE64_CHARS = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static inline const std::string ASCII_PRINTABLE = 
        "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
    
    // Unicode相关字符集
    static inline const std::vector<uint32_t> BASIC_LATIN_RANGE = {0x0020, 0x007F};
    static inline const std::vector<uint32_t> LATIN_1_RANGE = {0x00A0, 0x00FF};
    static inline const std::vector<uint32_t> GREEK_RANGE = {0x0370, 0x03FF};
    static inline const std::vector<uint32_t> CYRILLIC_RANGE = {0x0400, 0x04FF};
    static inline const std::vector<uint32_t> HIRAGANA_RANGE = {0x3040, 0x309F};
    static inline const std::vector<uint32_t> KATAKANA_RANGE = {0x30A0, 0x30FF};
    static inline const std::vector<uint32_t> CJK_UNIFIED_RANGE = {0x4E00, 0x9FFF};
    static inline const std::vector<uint32_t> EMOJI_RANGE = {0x1F600, 0x1F64F};
    
    // 密码学安全的随机数生成器
    class SecureRandom {
    private:
        static std::random_device& getDevice() {
            static std::random_device device;
            return device;
        }
        
    public:
        template<typename T>
        static T get() {
            T value;
            std::uniform_int_distribution<T> dist(
                std::numeric_limits<T>::min(),
                std::numeric_limits<T>::max()
            );
            return dist(getDevice());
        }
    };
};

} // namespace utils
} // namespace foundation

// 内联函数实现
#include "random_impl.hpp"