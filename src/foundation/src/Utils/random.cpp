// utils/random.cpp - 随机数工具非内联实现
#include "foundation/Utils/random.hpp"
#include <sstream>
#include <iomanip>
#include <array>
#include <variant>
#ifdef _WIN32
#include <Windows.h>
#include <ncrypt.h>
#include <wincrypt.h>
#elif
#endif
namespace foundation {
namespace utils {

std::string Random::getBase64String(size_t length) {
    std::string result;
    result.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        result.push_back(choice(BASE64_CHARS));
    }
    
    return result;
}

std::vector<uint8_t> Random::getSecureRandomBytes(size_t count) {
    std::vector<uint8_t> bytes(count);
    
    // 使用系统提供的密码学安全随机数生成器
    #ifdef _WIN32
        HCRYPTPROV hProvider = 0;
        if (CryptAcquireContext(&hProvider, nullptr, nullptr, 
                               PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
            CryptGenRandom(hProvider, static_cast<DWORD>(count), bytes.data());
            CryptReleaseContext(hProvider, 0);
        } else {
            // 回退到普通随机数
            std::generate(bytes.begin(), bytes.end(), 
                         []() { return getInt<uint8_t>(0, 255); });
        }
    #else
        // Unix-like 系统使用 /dev/urandom
        std::ifstream urandom("/dev/urandom", std::ios::binary);
        if (urandom.is_open()) {
            urandom.read(reinterpret_cast<char*>(bytes.data()), count);
        } else {
            // 回退到普通随机数
            std::generate(bytes.begin(), bytes.end(), 
                         []() { return getInt<uint8_t>(0, 255); });
        }
    #endif
    
    return bytes;
}

// std::string Random::getColorHex(bool withAlpha) {
//     auto color = withAlpha ? getColorWithAlpha() : getColor();
    
//     std::stringstream ss;
//     ss << std::hex << std::setfill('0');
    
//     if (withAlpha) {
//         auto [r, g, b, a] = color;
//         ss << std::setw(2) << static_cast<int>(r)
//            << std::setw(2) << static_cast<int>(g)
//            << std::setw(2) << static_cast<int>(b)
//            << std::setw(2) << static_cast<int>(a);
//     } else {
//         auto [r, g, b] = color;
//         ss << std::setw(2) << static_cast<int>(r)
//            << std::setw(2) << static_cast<int>(g)
//            << std::setw(2) << static_cast<int>(b);
//     }
    
//     return ss.str();
// }
std::string Random::getColorHex(bool withAlpha) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    if (withAlpha) {
        auto [r, g, b, a] = getColorWithAlpha();
        ss << std::setw(2) << static_cast<int>(r)
           << std::setw(2) << static_cast<int>(g)
           << std::setw(2) << static_cast<int>(b)
           << std::setw(2) << static_cast<int>(a);
    } else {
        auto [r, g, b] = getColor();
        ss << std::setw(2) << static_cast<int>(r)
           << std::setw(2) << static_cast<int>(g)
           << std::setw(2) << static_cast<int>(b);
    }
    
    return ss.str();
}
std::string Random::getIpAddress() {
    std::stringstream ss;
    ss << getInt(1, 223) << "."  // 不能是0.0.0.0或广播地址
       << getInt(0, 255) << "."
       << getInt(0, 255) << "."
       << getInt(1, 254);  // 不能是255
    
    return ss.str();
}

std::string Random::getMacAddress() {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    for (int i = 0; i < 6; ++i) {
        if (i > 0) ss << ":";
        ss << std::setw(2) << getInt(0, 255);
    }
    
    return ss.str();
}

std::string Random::getFilename(const std::string& extension) {
    std::string filename = getString(10, ALPHANUMERIC_LOWER);
    if (!extension.empty()) {
        filename += "." + extension;
    }
    return filename;
}

std::vector<std::string> Random::getStringArray(size_t count, 
                                               size_t minLength, 
                                               size_t maxLength) {
    std::vector<std::string> result;
    result.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        size_t length = getInt(minLength, maxLength);
        result.push_back(getString(length));
    }
    
    return result;
}

std::string Random::getName() {
    static const std::vector<std::string> firstNames = {
        "John", "Jane", "Bob", "Alice", "Charlie", "David", "Emma", "Frank",
        "Grace", "Henry", "Ivy", "Jack", "Kate", "Leo", "Mia", "Noah", "Olivia"
    };
    
    static const std::vector<std::string> lastNames = {
        "Smith", "Johnson", "Williams", "Brown", "Jones", "Garcia", "Miller",
        "Davis", "Rodriguez", "Martinez", "Hernandez", "Lopez", "Gonzalez"
    };
    
    return choice(firstNames) + " " + choice(lastNames);
}

std::string Random::getEmail() {
    static const std::vector<std::string> domains = {
        "gmail.com", "yahoo.com", "hotmail.com", "outlook.com", 
        "example.com", "test.com"
    };
    
    std::string username = getString(getInt(5, 10), ALPHANUMERIC_LOWER);
    std::string domain = choice(domains);
    
    return username + "@" + domain;
}

std::string Random::getPhoneNumber() {
    // 生成简单的手机号格式
    return "+1-" + std::to_string(getInt(200, 999)) + "-" +
           std::to_string(getInt(100, 999)) + "-" +
           std::to_string(getInt(1000, 9999));
}

std::string Random::getAddress() {
    static const std::vector<std::string> streets = {
        "Main St", "Oak Ave", "Maple Rd", "Elm St", "Pine St", "Cedar Ln"
    };
    
    static const std::vector<std::string> cities = {
        "New York", "Los Angeles", "Chicago", "Houston", "Phoenix", "Philadelphia"
    };
    
    static const std::vector<std::string> states = {
        "CA", "NY", "TX", "FL", "IL", "PA", "OH", "GA", "NC", "MI"
    };
    
    std::stringstream ss;
    ss << getInt(100, 9999) << " " << choice(streets) << "\n"
       << choice(cities) << ", " << choice(states) << " "
       << getInt(10000, 99999);
    
    return ss.str();
}

std::string Random::getDate(int startYear, int endYear) {
    int year = getInt(startYear, endYear);
    int month = getInt(1, 12);
    int day = getInt(1, 28); // 简化，假设每个月都是28天
    
    std::stringstream ss;
    ss << std::setfill('0') 
       << year << "-"
       << std::setw(2) << month << "-"
       << std::setw(2) << day;
    
    return ss.str();
}

std::string Random::getTime() {
    std::stringstream ss;
    ss << std::setfill('0')
       << std::setw(2) << getInt(0, 23) << ":"
       << std::setw(2) << getInt(0, 59) << ":"
       << std::setw(2) << getInt(0, 59);
    
    return ss.str();
}

std::string Random::getDateTime(int startYear, int endYear) {
    return getDate(startYear, endYear) + " " + getTime();
}

std::string Random::getTextContent(size_t minWords, size_t maxWords) {
    static const std::vector<std::string> words = {
        "the", "quick", "brown", "fox", "jumps", "over", "lazy", "dog",
        "hello", "world", "foundation", "library", "random", "text",
        "generation", "is", "useful", "for", "testing", "purposes",
        "this", "is", "sample", "text", "that", "can", "be", "used",
        "in", "various", "applications", "requiring", "dummy", "content"
    };
    
    size_t wordCount = getInt(minWords, maxWords);
    std::stringstream ss;
    
    for (size_t i = 0; i < wordCount; ++i) {
        if (i > 0) {
            if (getBool(0.1)) {
                ss << ". ";
            } else {
                ss << " ";
            }
        }
        ss << choice(words);
    }
    ss << ".";
    
    return ss.str();
}

std::string Random::getJsonData(size_t minItems, size_t maxItems) {
    size_t itemCount = getInt(minItems, maxItems);
    std::stringstream ss;
    
    ss << "{\n";
    for (size_t i = 0; i < itemCount; ++i) {
        if (i > 0) ss << ",\n";
        
        std::string key = getString(getInt(5, 10), ALPHANUMERIC_LOWER);
        ss << "  \"" << key << "\": ";
        
        // 随机选择值类型
        int type = getInt(0, 3);
        switch (type) {
            case 0: // 字符串
                ss << "\"" << getString(getInt(5, 15)) << "\"";
                break;
            case 1: // 整数
                ss << getInt(0, 1000);
                break;
            case 2: // 浮点数
                ss << getFloat(0.0, 1000.0);
                break;
            case 3: // 布尔值
                ss << (getBool() ? "true" : "false");
                break;
        }
    }
    ss << "\n}";
    
    return ss.str();
}

std::string Random::getCsvData(size_t rows, size_t cols) {
    std::stringstream ss;
    
    // 标题行
    for (size_t col = 0; col < cols; ++col) {
        if (col > 0) ss << ",";
        ss << "Column" << (col + 1);
    }
    ss << "\n";
    
    // 数据行
    for (size_t row = 0; row < rows; ++row) {
        for (size_t col = 0; col < cols; ++col) {
            if (col > 0) ss << ",";
            
            int type = getInt(0, 2);
            switch (type) {
                case 0: // 字符串
                    ss << getString(getInt(5, 10));
                    break;
                case 1: // 整数
                    ss << getInt(0, 1000);
                    break;
                case 2: // 浮点数
                    ss << getFloat(0.0, 1000.0);
                    break;
            }
        }
        ss << "\n";
    }
    
    return ss.str();
}

} // namespace utils
} // namespace foundation