// src/market/core/DataTypes.cpp
#include "core/DataType.h"
#include <sstream>
#include <iomanip>
#include <cmath>
namespace domain {
namespace market {

// // ============== KLine 方法实现 ==============

// double KLine::change_rate() const {
//     if (open == 0.0) return 0.0;
//     return (close - open) / open;
// }

// bool KLine::is_yang() const {
//     return close > open;
// }

// double KLine::amplitude() const {
//     if (open == 0.0) return 0.0;
//     return (high - low) / open;
// }

// bool KLine::is_valid() const {
//     return timestamp > 0 && 
//            open > 0 && 
//            high >= low && 
//            high >= open && 
//            high >= close &&
//            low <= open && 
//            low <= close &&
//            volume >= 0 && 
//            amount >= 0;
// }

// std::string KLine::to_string() const {
//     std::ostringstream oss;
//     oss << std::fixed << std::setprecision(4)
//         << "KLine{symbol_id:" << symbol_id
//         << ", period:" << period
//         << ", timestamp:" << timestamp
//         << ", O:" << open
//         << ", H:" << high
//         << ", L:" << low
//         << ", C:" << close
//         << ", V:" << volume
//         << ", A:" << amount
//         << ", T:" << turnover << "}";
//     return oss.str();
// }

// ============== TickData 方法实现 ==============

// double TickData::spread() const {
//     if (bid_prices[0] == 0.0 || ask_prices[0] == 0.0) return 0.0;
//     return ask_prices[0] - bid_prices[0];
// }

// double TickData::mid_price() const {
//     if (bid_prices[0] == 0.0 || ask_prices[0] == 0.0) {
//         return price > 0 ? price : 0.0;
//     }
//     return (ask_prices[0] + bid_prices[0]) * 0.5;
// }

// bool TickData::is_buy() const {
//     return direction > 0;
// }

// bool TickData::is_sell() const {
//     return direction < 0;
// }

// bool TickData::is_valid() const {
//     return timestamp > 0 && price > 0 && volume >= 0;
// }

// std::string TickData::to_string() const {
//     std::ostringstream oss;
//     oss << std::fixed << std::setprecision(4)
//         << "TickData{symbol_id:" << symbol_id
//         << ", timestamp:" << timestamp
//         << ", price:" << price
//         << ", volume:" << volume
//         << ", direction:" << direction << "}";
//     return oss.str();
// }

// // ============== DepthData 方法实现 ==============

// double DepthData::total_bid_volume() const {
//     double total = 0.0;
//     for (double vol : bid_volumes) {
//         total += vol;
//     }
//     return total;
// }

// double DepthData::total_ask_volume() const {
//     double total = 0.0;
//     for (double vol : ask_volumes) {
//         total += vol;
//     }
//     return total;
// }

// double DepthData::imbalance() const {
//     double bid_total = total_bid_volume();
//     double ask_total = total_ask_volume();
    
//     if (bid_total + ask_total == 0.0) return 0.0;
    
//     return (bid_total - ask_total) / (bid_total + ask_total);
// }

// bool DepthData::is_valid() const {
//     return timestamp > 0 &&
//            !bid_prices.empty() && 
//            !ask_prices.empty() &&
//            bid_prices.size() == bid_volumes.size() &&
//            ask_prices.size() == ask_volumes.size();
// }

// ============== KLineBatch 方法实现 ==============

KLineBatch::KLineBatch(size_t capacity) : data_(capacity), size_(0) {}

// KLineBatch::KLineBatch(KLineBatch&& other) noexcept
//     : data_(std::move(other.data_))
//     , size_(other.size_) {
//     other.size_ = 0;
// }

// KLineBatch& KLineBatch::operator=(KLineBatch&& other) noexcept {
//     if (this != &other) {
//         data_ = std::move(other.data_);
//         size_ = other.size_;
//         other.size_ = 0;
//     }
//     return *this;
// }

void KLineBatch::push_back(const KLine& kline) {
    if (size_ >= data_.size()) {
        data_.resize(data_.size() * 2);
    }
    data_[size_++] = kline;
}

const KLine& KLineBatch::operator[](size_t index) const {
    if (index >= size_) {
        throw std::out_of_range("KLineBatch index out of range");
    }
    return data_[index];
}

KLine& KLineBatch::operator[](size_t index) {
    if (index >= size_) {
        throw std::out_of_range("KLineBatch index out of range");
    }
    return data_[index];
}

void KLineBatch::clear() noexcept {
    size_ = 0;
}

void KLineBatch::shrink_to_fit() {
    if (size_ < data_.size()) {
        data_.resize(size_);
    }
}

} // namespace market
} // namespace astock