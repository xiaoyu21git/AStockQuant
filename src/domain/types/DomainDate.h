#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace domain {

/// 域层统一日期类型，基于 int32_t YYYYMMDD 紧凑表示，零 Qt 依赖
struct DomainDate final {
    int32_t value{0};

    static constexpr int32_t kMinimumDate = 19000101;
    static constexpr int32_t kMaximumDate = 29991231;

    [[nodiscard]] bool isValid() const noexcept
    {
        return value >= kMinimumDate && value <= kMaximumDate;
    }

    [[nodiscard]] bool operator==(const DomainDate& other) const noexcept
    {
        return value == other.value;
    }

    [[nodiscard]] bool operator!=(const DomainDate& other) const noexcept
    {
        return value != other.value;
    }

    [[nodiscard]] bool operator<(const DomainDate& other) const noexcept
    {
        return value < other.value;
    }

    [[nodiscard]] bool operator<=(const DomainDate& other) const noexcept
    {
        return value <= other.value;
    }

    [[nodiscard]] bool operator>(const DomainDate& other) const noexcept
    {
        return value > other.value;
    }

    [[nodiscard]] bool operator>=(const DomainDate& other) const noexcept
    {
        return value >= other.value;
    }

    /// 转换为 ISO 8601 字符串 "YYYY-MM-DD"
    [[nodiscard]] std::string toIsoString() const
    {
        if (!isValid()) return "";
        const int32_t y = value / 10000;
        const int32_t m = (value / 100) % 100;
        const int32_t d = value % 100;
        char buf[11];
        buf[0] = static_cast<char>('0' + y / 1000);
        buf[1] = static_cast<char>('0' + (y / 100) % 10);
        buf[2] = static_cast<char>('0' + (y / 10) % 10);
        buf[3] = static_cast<char>('0' + y % 10);
        buf[4] = '-';
        buf[5] = static_cast<char>('0' + m / 10);
        buf[6] = static_cast<char>('0' + m % 10);
        buf[7] = '-';
        buf[8] = static_cast<char>('0' + d / 10);
        buf[9] = static_cast<char>('0' + d % 10);
        buf[10] = '\0';
        return std::string(buf);
    }
};

/// 域层统一时间戳（epoch 秒，int64_t）
struct DomainDateTime final {
    int64_t epochSeconds{0};

    [[nodiscard]] bool isValid() const noexcept
    {
        return epochSeconds > 0;
    }
};

/// 诊断键值对，替代 QVariantMap
struct DiagnosticRecord final {
    std::string key;
    std::string value;
};

using DiagnosticMap = std::vector<DiagnosticRecord>;

inline std::string diagnosticGet(const DiagnosticMap& map, const std::string& key,
                                  const std::string& defaultValue = {})
{
    for (const auto& rec : map) {
        if (rec.key == key) return rec.value;
    }
    return defaultValue;
}

inline void diagnosticSet(DiagnosticMap& map, const std::string& key, const std::string& value)
{
    for (auto& rec : map) {
        if (rec.key == key) {
            rec.value = value;
            return;
        }
    }
    map.push_back({key, value});
}

} // namespace domain