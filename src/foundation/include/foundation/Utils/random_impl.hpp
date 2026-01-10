// utils/random_impl.hpp - 随机数工具内联实现
#pragma once

#include "random.hpp"
#include <algorithm>
#include <numeric>
#include <iomanip>
#include <iostream>
#include <iosfwd>
#include <sstream>
namespace foundation {
namespace utils {

// 字符集定义
// const std::string Random::ALPHANUMERIC = 
//     "0123456789"
//     "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
//     "abcdefghijklmnopqrstuvwxyz";
    
// const std::string Random::ALPHANUMERIC_LOWER = 
//     "0123456789abcdefghijklmnopqrstuvwxyz";
    
// const std::string Random::ALPHANUMERIC_UPPER = 
//     "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    
// const std::string Random::HEX_LOWER = "0123456789abcdef";
// const std::string Random::HEX_UPPER = "0123456789ABCDEF";
    
// const std::string Random::BASE64_CHARS = 
//     "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
//     "abcdefghijklmnopqrstuvwxyz"
//     "0123456789+/";
    
// const std::string Random::LETTERS = 
//     "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
//     "abcdefghijklmnopqrstuvwxyz";
    
// const std::string Random::LETTERS_LOWER = "abcdefghijklmnopqrstuvwxyz";
// const std::string Random::LETTERS_UPPER = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
// const std::string Random::DIGITS = "0123456789";

// ============ 内联函数实现 ============

inline std::string Random::getString(size_t length) {
    return getString(length, ALPHANUMERIC);
}

inline std::string Random::getString(size_t length, const std::string& charset) {
    if (charset.empty() || length == 0) {
        return "";
    }
    
    std::uniform_int_distribution<unsigned int> dist(0, charset.size() - 1);
    std::string result;
    result.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        result.push_back(charset[dist(getGenerator())]);
    }
    
    return result;
}

inline std::string Random::getHexString(size_t length, bool uppercase) {
    const auto& charset = uppercase ? HEX_UPPER : HEX_LOWER;
    return getString(length, charset);
}

inline std::string Random::generateUuid() {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    // 生成32个十六进制字符
    for (int i = 0; i < 32; ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) {
            ss << '-';
        }
        ss << std::setw(1) << getInt(0, 15);
    }
    
    // 设置UUID版本4和第4部分的变体
    std::string uuid = ss.str();
    uuid[14] = '4';  // 版本4
    uuid[19] = '8';  // 变体1 (10xx)
    
    return uuid;
}

inline std::string Random::generateSimpleUuid() {
    return getHexString(32);
}

template<typename Container>
inline std::vector<typename Container::value_type> Random::sample(
    const Container& container, size_t count) {
    
    if (count > container.size()) {
        throw std::invalid_argument("Sample size exceeds container size");
    }
    
    std::vector<typename Container::value_type> result;
    std::vector<size_t> indices(container.size());
    std::iota(indices.begin(), indices.end(), 0);
    
    // 随机选择索引
    shuffle(indices);
    
    // 收集结果
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        auto it = container.begin();
        std::advance(it, indices[i]);
        result.push_back(*it);
    }
    
    return result;
}

template<typename Container>
inline std::vector<typename Container::value_type> Random::choices(
    const Container& container, size_t count) {
    
    std::vector<typename Container::value_type> result;
    result.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        result.push_back(choice(container));
    }
    
    return result;
}

template<typename T>
inline T Random::getNormal(T mean, T stddev) {
    std::normal_distribution<T> dist(mean, stddev);
    return dist(getGenerator());
}

template<typename T>
inline T Random::getExponential(T lambda) {
    std::exponential_distribution<T> dist(lambda);
    return dist(getGenerator());
}

inline int Random::getPoisson(double lambda) {
    std::poisson_distribution<int> dist(lambda);
    return dist(getGenerator());
}

inline int Random::getBinomial(int trials, double probability) {
    std::binomial_distribution<int> dist(trials, probability);
    return dist(getGenerator());
}

inline int Random::getGeometric(double probability) {
    std::geometric_distribution<int> dist(probability);
    return dist(getGenerator());
}

template<typename Container>
inline void Random::shuffle(Container& container) {
    std::shuffle(container.begin(), container.end(), getGenerator());
}

template<typename T>
inline std::vector<T> Random::permutation(size_t n) {
    std::vector<T> result(n);
    std::iota(result.begin(), result.end(), static_cast<T>(0));
    shuffle(result);
    return result;
}

inline void Random::seed(unsigned int seed) {
    getGenerator().seed(seed);
}

inline void Random::seedWithTime() {
    auto now = std::chrono::high_resolution_clock::now();
    auto seed = static_cast<unsigned int>(
        now.time_since_epoch().count());
    getGenerator().seed(seed);
}

inline void Random::seedWithRandomDevice() {
    std::random_device rd;
    getGenerator().seed(rd());
}

template<typename T, typename>
inline T Random::getSecureRandomInt(T min, T max) {
    // 使用安全的中间类型
    using SafeType = typename std::conditional<
        sizeof(T) <= sizeof(int), int,
        typename std::conditional<
            sizeof(T) <= sizeof(long), long,
            long long
        >::type
    >::type;
    
    std::uniform_int_distribution<SafeType> dist(
        static_cast<SafeType>(min), 
        static_cast<SafeType>(max)
    );
    return static_cast<T>(dist(SecureRandom::getDevice()));
}

inline std::tuple<uint8_t, uint8_t, uint8_t> Random::getColor() {
    return {
        static_cast<uint8_t>(getInt(0, 255)),
        static_cast<uint8_t>(getInt(0, 255)),
        static_cast<uint8_t>(getInt(0, 255))
    };
}

inline std::tuple<uint8_t, uint8_t, uint8_t, uint8_t> Random::getColorWithAlpha() {
    return {
        static_cast<uint8_t>(getInt(0, 255)),
        static_cast<uint8_t>(getInt(0, 255)),
        static_cast<uint8_t>(getInt(0, 255)),
        static_cast<uint8_t>(getInt(0, 255))
    };
}

template<typename T>
inline T Random::weightedChoice(const std::vector<T>& items, 
                               const std::vector<double>& weights) {
    if (items.size() != weights.size()) {
        throw std::invalid_argument("Items and weights must have same size");
    }
    
    std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
    return items[dist(getGenerator())];
}

template<typename T>
inline T Random::rouletteWheel(const std::vector<T>& items,
                              const std::vector<double>& weights) {
    return weightedChoice(items, weights);
}

template<typename T>
inline std::vector<T> Random::getIntArray(size_t count, T min, T max) {
    std::vector<T> result;
    result.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        result.push_back(getInt(min, max));
    }
    
    return result;
}

template<typename T>
inline std::vector<T> Random::getFloatArray(size_t count, T min, T max) {
    std::vector<T> result;
    result.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        result.push_back(getFloat(min, max));
    }
    
    return result;
}

} // namespace utils
} // namespace foundation