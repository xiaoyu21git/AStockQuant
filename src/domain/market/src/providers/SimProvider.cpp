// src/market/providers/SimProvider.cpp
#include "market/providers/SimProvider.h"

#include <chrono>
#include <random>

namespace astock::market {

SimProvider::SimProvider(const std::string& config)
    : config_(config) {
    // 极简配置解析：查找 base_price= 和 update_interval_ms=
    auto pos = config.find("base_price=");
    if (pos != std::string::npos) {
        base_price_ = std::stod(config.substr(pos + 11));
    }
    pos = config.find("update_interval_ms=");
    if (pos != std::string::npos) {
        update_interval_ms_ = static_cast<std::uint64_t>(
            std::stoull(config.substr(pos + 18)));
    }
}

ProviderStatus SimProvider::get_status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

bool SimProvider::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = ProviderStatus::CONNECTED;
    return true;
}

void SimProvider::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = ProviderStatus::DISCONNECTED;
}

void SimProvider::register_kline_callback(KLineCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    kline_cb_ = std::move(cb);
}

void SimProvider::register_tick_callback(TickCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    tick_cb_ = std::move(cb);
}

bool SimProvider::subscribe_kline(std::uint32_t /*symbol_id*/, std::uint32_t /*period*/) {
    // 目前订阅不驱动实时推送，仅用于接口兼容
    return true;
}

bool SimProvider::unsubscribe_kline(std::uint32_t /*symbol_id*/, std::uint32_t /*period*/) {
    return true;
}

bool SimProvider::subscribe_tick(std::uint32_t /*symbol_id*/) {
    return true;
}

bool SimProvider::unsubscribe_tick(std::uint32_t /*symbol_id*/) {
    return true;
}

KLineBatch SimProvider::get_history_klines(
    std::uint32_t symbol_id,
    std::uint32_t period,
    std::uint64_t start_time,
    std::uint64_t end_time,
    std::size_t  limit) {

    if (limit == 0) {
        limit = 1;
    }

    KLineBatch batch(limit);

    const std::uint64_t interval = period * 60ULL; // 秒
    std::uint64_t ts = start_time ? start_time : 1700000000ULL;

    std::mt19937_64 rng{static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count())};
    std::normal_distribution<double> noise{0.0, 0.5};

    double price = base_price_;

    for (std::size_t i = 0; i < limit; ++i) {
        if (end_time && ts > end_time) {
            break;
        }

        double open  = price;
        double close = price + noise(rng);
        double high  = std::max(open, close) + 0.5;
        double low   = std::min(open, close) - 0.5;

        KLine k;
        k.symbol_id = symbol_id;
        k.period    = period;
        k.timestamp = ts;
        k.open      = open;
        k.high      = high;
        k.low       = low;
        k.close     = close;
        k.volume    = 100000.0f;
        k.amount    = static_cast<float>(k.close * k.volume);
        k.turnover  = 1.0f;

        batch.push_back(k);

        price = close;
        ts += interval;
    }

    return batch;
}

KLineBatch SimProvider::get_history_klines_batch(
    const std::vector<std::uint32_t>& symbol_ids,
    const std::uint32_t period,
    const std::uint64_t start_time,
    const std::uint64_t end_time,
    const std::size_t limit_per_symbol)
{
    KLineBatch batch(symbol_ids.size() * (limit_per_symbol == 0U ? 1U : limit_per_symbol));
    for (const std::uint32_t symbol_id : symbol_ids) {
        KLineBatch symbolBatch = get_history_klines(symbol_id,
                                                    period,
                                                    start_time,
                                                    end_time,
                                                    limit_per_symbol);
        for (const KLine& kline : symbolBatch) {
            batch.push_back(kline);
        }
    }

    return batch;
}

} // namespace astock::market
