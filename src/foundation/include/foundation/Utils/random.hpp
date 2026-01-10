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
    // 预定义字符集
    // static const std::string ALPHANUMERIC;
    // static const std::string ALPHANUMERIC_LOWER;
    // static const std::string ALPHANUMERIC_UPPER;
    // static const std::string HEX_LOWER;
    // static const std::string HEX_UPPER;
    // static const std::string BASE64_CHARS;
    // static const std::string LETTERS;
    // static const std::string LETTERS_LOWER;
    // static const std::string LETTERS_UPPER;
    // static const std::string DIGITS;
     // C++17 内联变量定义
        static inline const std::string ALPHANUMERIC = 
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        static inline const std::string ALPHANUMERIC_LOWER = 
            "0123456789abcdefghijklmnopqrstuvwxyz";
        static inline const std::string ALPHANUMERIC_UPPER = 
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        static inline const std::string HEX_LOWER = 
            "0123456789abcdef";
        static inline const std::string HEX_UPPER = 
            "0123456789ABCDEF";
        static inline const std::string BASE64_CHARS = 
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        static inline const std::string LETTERS = 
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        static inline const std::string LETTERS_LOWER = 
            "abcdefghijklmnopqrstuvwxyz";
        static inline const std::string LETTERS_UPPER = 
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        static inline const std::string DIGITS = 
            "0123456789";
    
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