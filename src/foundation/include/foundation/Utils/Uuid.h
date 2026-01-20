#pragma once

#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <iosfwd>
#include <functional>
#include <cstring>

namespace foundation {
namespace utils {

class Uuid {
public:
    // 构造函数
    Uuid();
    explicit Uuid(const std::string& str);
    explicit Uuid(const uint8_t bytes[16]);
    Uuid(const Uuid& other);
    Uuid(Uuid&& other) noexcept;
    const uint8_t* data_ptr() const noexcept { return data; }
    // 赋值操作符
    Uuid& operator=(const Uuid& other);
    Uuid& operator=(Uuid&& other) noexcept;
    Uuid& operator=(const std::string& str);
    
    // 比较操作符
    bool operator==(const Uuid& other) const;
    bool operator!=(const Uuid& other) const;
    bool operator<(const Uuid& other) const;
    bool operator<=(const Uuid& other) const;
    bool operator>(const Uuid& other) const;
    bool operator>=(const Uuid& other) const;
    
    // 类型转换
    explicit operator bool() const;
    explicit operator std::string() const;
    
    // 字符串操作
    std::string to_string() const;
    std::string to_string_no_dashes() const;
    std::string to_urn_string() const;
    
    // 字节操作
    void to_bytes(uint8_t bytes[16]) const;
    std::vector<uint8_t> to_byte_vector() const;
    std::array<uint8_t, 16> to_byte_array() const;
    
    // 状态查询
    bool is_null() const;
    bool is_valid() const;
    int version() const;    // 返回UUID版本 (1-5, 4表示随机生成)
    int variant() const;    // 返回UUID变体
    
    // 工厂方法
    static Uuid generate();                         // 默认生成v4
    static Uuid generate_v4();                      // 生成版本4的UUID
    static Uuid from_string(const std::string& str);
    static Uuid from_bytes(const uint8_t bytes[16]);
    static Uuid from_byte_vector(const std::vector<uint8_t>& bytes);
    static Uuid null();                             // 返回空UUID
    
    // 验证方法
    static bool is_valid_uuid(const std::string& str);
    
private:
    void generate_v4_impl();
    void parse(const std::string& str);
    void clear();
    static bool is_hex_char(char c);
    
private:
    uint8_t data[16];
};
// Uuid 的哈希函数
struct UuidHash {
    size_t operator()(const Uuid& uuid) const noexcept {
        uint64_t high = 0, low = 0;
        const uint8_t* p = uuid.data_ptr();

        std::memcpy(&high, p, 8);
        std::memcpy(&low, p + 8, 8);

        size_t h1 = std::hash<uint64_t>{}(high);
        size_t h2 = std::hash<uint64_t>{}(low);
        return h1 ^ (h2 << 1);
    }
};
// 流操作符
std::ostream& operator<<(std::ostream& os, const Uuid& uuid);
std::istream& operator>>(std::istream& is, Uuid& uuid);

} // namespace utils
} // namespace engine
// 标准库特化
namespace std {
    template<>
    struct hash<foundation::utils::Uuid> : foundation::utils::UuidHash {};
}