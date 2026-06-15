#pragma once
// ═════════════════════════════════════════════════════════════════════════
// InstrumentId — 域层统一标的标识符 (纯 C++，零 Qt)
// 替代 domain/factor/、domain/backtest/ 中多处重复定义
// ═════════════════════════════════════════════════════════════════════════

#include <cstdint>

namespace domain {

struct InstrumentId final {
    std::uint32_t value{0};

    InstrumentId() = default;
    explicit InstrumentId(std::uint32_t v) : value(v) {}

    [[nodiscard]] bool isValid() const noexcept { return value > 0; }

    friend bool operator==(InstrumentId a, InstrumentId b) noexcept { return a.value == b.value; }
    friend bool operator!=(InstrumentId a, InstrumentId b) noexcept { return a.value != b.value; }
    friend bool operator<(InstrumentId a, InstrumentId b) noexcept  { return a.value < b.value; }

    struct Hash {
        std::size_t operator()(InstrumentId id) const noexcept {
            return std::hash<std::uint32_t>{}(id.value);
        }
    };
};

} // namespace domain
