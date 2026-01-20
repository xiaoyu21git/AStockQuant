#include "foundation/Utils/Uuid.h"
#include <foundation.h>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <random>
#include <chrono>

namespace foundation {
namespace utils {

// ============================================================================
// 构造函数
// ============================================================================

Uuid::Uuid() {
    generate();
}

Uuid::Uuid(const std::string& str) {
    if (is_valid_uuid(str)) {
        parse(str);
    } else {
        clear(); // 无效UUID设为全0
    }
}

Uuid::Uuid(const uint8_t bytes[16]) {
    std::memcpy(data, bytes, 16);
}

Uuid::Uuid(const Uuid& other) {
    std::memcpy(data, other.data, 16);
}

Uuid::Uuid(Uuid&& other) noexcept {
    std::memcpy(data, other.data, 16);
    other.clear();
}

// ============================================================================
// 赋值操作符
// ============================================================================

Uuid& Uuid::operator=(const Uuid& other) {
    if (this != &other) {
        std::memcpy(data, other.data, 16);
    }
    return *this;
}

Uuid& Uuid::operator=(Uuid&& other) noexcept {
    if (this != &other) {
        std::memcpy(data, other.data, 16);
        other.clear();
    }
    return *this;
}

Uuid& Uuid::operator=(const std::string& str) {
    if (is_valid_uuid(str)) {
        parse(str);
    } else {
        clear();
    }
    return *this;
}

// ============================================================================
// 比较操作符
// ============================================================================

bool Uuid::operator==(const Uuid& other) const {
    return std::memcmp(data, other.data, 16) == 0;
}

bool Uuid::operator!=(const Uuid& other) const {
    return !(*this == other);
}

bool Uuid::operator<(const Uuid& other) const {
    return std::memcmp(data, other.data, 16) < 0;
}

bool Uuid::operator<=(const Uuid& other) const {
    return std::memcmp(data, other.data, 16) <= 0;
}

bool Uuid::operator>(const Uuid& other) const {
    return std::memcmp(data, other.data, 16) > 0;
}

bool Uuid::operator>=(const Uuid& other) const {
    return std::memcmp(data, other.data, 16) >= 0;
}

// ============================================================================
// 类型转换
// ============================================================================

Uuid::operator bool() const {
    return !is_null();
}

Uuid::operator std::string() const {
    return to_string();
}

// ============================================================================
// 字符串操作
// ============================================================================

std::string Uuid::to_string() const {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    // 格式: 8-4-4-4-12
    oss << std::setw(2) << static_cast<int>(data[0])
        << std::setw(2) << static_cast<int>(data[1])
        << std::setw(2) << static_cast<int>(data[2])
        << std::setw(2) << static_cast<int>(data[3]);
    oss << '-';
    oss << std::setw(2) << static_cast<int>(data[4])
        << std::setw(2) << static_cast<int>(data[5]);
    oss << '-';
    oss << std::setw(2) << static_cast<int>(data[6])
        << std::setw(2) << static_cast<int>(data[7]);
    oss << '-';
    oss << std::setw(2) << static_cast<int>(data[8])
        << std::setw(2) << static_cast<int>(data[9]);
    oss << '-';
    oss << std::setw(2) << static_cast<int>(data[10])
        << std::setw(2) << static_cast<int>(data[11])
        << std::setw(2) << static_cast<int>(data[12])
        << std::setw(2) << static_cast<int>(data[13])
        << std::setw(2) << static_cast<int>(data[14])
        << std::setw(2) << static_cast<int>(data[15]);
    
    return oss.str();
}

std::string Uuid::to_string_no_dashes() const {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    for (int i = 0; i < 16; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    
    return oss.str();
}

std::string Uuid::to_urn_string() const {
    return "urn:uuid:" + to_string();
}

// ============================================================================
// 字节操作
// ============================================================================

void Uuid::to_bytes(uint8_t bytes[16]) const {
    std::memcpy(bytes, data, 16);
}

std::vector<uint8_t> Uuid::to_byte_vector() const {
    return std::vector<uint8_t>(data, data + 16);
}

std::array<uint8_t, 16> Uuid::to_byte_array() const {
    std::array<uint8_t, 16> arr;
    std::copy(data, data + 16, arr.begin());
    return arr;
}

// ============================================================================
// 状态查询
// ============================================================================

bool Uuid::is_null() const {
    static const uint8_t zero[16] = {0};
    return std::memcmp(data, zero, 16) == 0;
}

bool Uuid::is_valid() const {
    return !is_null();
}

int Uuid::version() const {
    // 版本位在字节6的高4位 (data[6] & 0xF0)
    return (data[6] & 0xF0) >> 4;
}

int Uuid::variant() const {
    // 变体位在字节8的高3位 (data[8] & 0xE0)
    uint8_t v = data[8] & 0xE0;
    if ((v & 0x80) == 0x00) return 0;  // NCS
    if ((v & 0xC0) == 0x80) return 1;  // RFC 4122
    if ((v & 0xE0) == 0xC0) return 2;  // Microsoft
    return 3;                           // Future
}

// ============================================================================
// 工厂方法
// ============================================================================

Uuid Uuid::generate() {
    return Uuid();
}

Uuid Uuid::generate_v4() {
    Uuid uuid;
    uuid.generate_v4_impl();
    return uuid;
}

Uuid Uuid::from_string(const std::string& str) {
    return Uuid(str);
}

Uuid Uuid::from_bytes(const uint8_t bytes[16]) {
    return Uuid(bytes);
}

Uuid Uuid::from_byte_vector(const std::vector<uint8_t>& bytes) {
    if (bytes.size() != 16) {
        return Uuid::null();
    }
    return Uuid(bytes.data());
}

Uuid Uuid::null() {
    Uuid uuid;
    uuid.clear();
    return uuid;
}

// ============================================================================
// 验证方法
// ============================================================================

bool Uuid::is_valid_uuid(const std::string& str) {
    // UUID格式：xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (36字符)
    // 或：xxxxxxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (32字符，无破折号)
    
    if (str.length() == 36) {
        // 带破折号的格式
        for (size_t i = 0; i < str.length(); i++) {
            char c = str[i];
            
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                if (c != '-') return false;
            } else {
                if (!is_hex_char(c)) return false;
            }
        }
        return true;
    } else if (str.length() == 32) {
        // 无破折号的格式
        for (char c : str) {
            if (!is_hex_char(c)) return false;
        }
        return true;
    }
    
    return false;
}

// ============================================================================
// 私有方法
// ============================================================================


void Uuid::generate_v4_impl() {
    // 使用时间戳和随机数生成UUID v4
    static std::mt19937_64 rng(std::random_device{}() ^ 
                              std::chrono::system_clock::now().time_since_epoch().count());
    static std::uniform_int_distribution<uint32_t> dist(0, 255);
    
    // 生成16个随机字节
    for (int i = 0; i < 16; ++i) {
        data[i] = dist(rng);
    }
    
    // 设置版本位 (第6字节的高4位为0100，表示版本4)
    data[6] = (data[6] & 0x0F) | 0x40;
    
    // 设置变体位 (第8字节的高2位为10，表示RFC 4122)
    data[8] = (data[8] & 0x3F) | 0x80;
}

void Uuid::parse(const std::string& str) {
    std::string clean_str;
    
    // 移除破折号
    for (char c : str) {
        if (c != '-') {
            clean_str.push_back(c);
        }
    }
    
    // 解析32个十六进制字符
    for (int i = 0; i < 16; ++i) {
        std::string byte_str = clean_str.substr(i * 2, 2);
        data[i] = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
    }
}

void Uuid::clear() {
    std::memset(data, 0, 16);
}

bool Uuid::is_hex_char(char c) {
    return (c >= '0' && c <= '9') || 
           (c >= 'a' && c <= 'f') || 
           (c >= 'A' && c <= 'F');
}

// ============================================================================
// 流操作符
// ============================================================================

std::ostream& operator<<(std::ostream& os, const Uuid& uuid) {
    os << uuid.to_string();
    return os;
}

std::istream& operator>>(std::istream& is, Uuid& uuid) {
    std::string str;
    is >> str;
    uuid = Uuid::from_string(str);
    return is;
}

} // namespace utils
} // namespace engine