#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>

namespace astock::market {

// K 线结构，按 64 字节对齐以提升缓存友好性
struct alignas(64) KLine {
    std::uint32_t symbol_id{0};
    std::uint16_t period{0};
    std::uint16_t _padding{0}; // 保持 8 字节对齐

    std::uint64_t timestamp{0};

    double open{0.0};
    double high{0.0};
    double low{0.0};
    double close{0.0};

    float volume{0.0f};
    float amount{0.0f};
    float turnover{0.0f};
    float _reserved{0.0f};

    double change_rate() const;
    bool   is_yang() const;
    double amplitude() const;
    bool   is_valid() const;
    std::string to_string() const;
};

static_assert(alignof(KLine) == 64, "KLine alignment must be 64");
static_assert(sizeof(KLine) == 64,  "KLine size must be 64 bytes");

struct TickData {
    std::uint64_t timestamp{0};
    std::uint64_t sequence{0};
    std::uint32_t symbol_id{0};
    std::int32_t  direction{0};  // 1: buy, -1: sell, 0: unknown

    double price{0.0};
    double volume{0.0};
    double amount{0.0};

    std::array<double, 5> bid_prices{};
    std::array<double, 5> bid_volumes{};
    std::array<double, 5> ask_prices{};
    std::array<double, 5> ask_volumes{};

    double spread() const;
    double mid_price() const;
    bool   is_buy() const;
    bool   is_sell() const;
    bool   is_valid() const;
    std::string to_string() const;
};

static_assert(sizeof(TickData) ==
              sizeof(std::uint64_t) * 2 +
              sizeof(std::uint32_t) +
              sizeof(double) * 3 +
              sizeof(std::int32_t) +
              sizeof(double) * 20,
              "TickData layout does not match expectation");

struct DepthData {
    std::uint32_t symbol_id{0};
    std::uint64_t timestamp{0};

    std::vector<double> bid_prices;
    std::vector<double> bid_volumes;
    std::vector<double> ask_prices;
    std::vector<double> ask_volumes;

    double total_bid_volume() const;
    double total_ask_volume() const;
    double imbalance() const;
    bool   is_valid() const;
};

class KLineBatch {
public:
    using value_type = KLine;

    explicit KLineBatch(std::size_t capacity = 0);

    void push_back(const KLine& kline);

    const KLine& operator[](std::size_t index) const;
    KLine&       operator[](std::size_t index);

    std::size_t size() const noexcept { return size_; }
    bool        empty() const noexcept { return size_ == 0; }

    void clear() noexcept;
    void shrink_to_fit();

    const KLine* begin() const noexcept { return data_.data(); }
    const KLine* end()   const noexcept { return data_.data() + size_; }

private:
    std::vector<KLine> data_;
    std::size_t        size_{0};
};

} // namespace astock::market
