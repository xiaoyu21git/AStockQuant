#pragma once

#include <string>
#include <mutex>
#include <functional>
#include <optional>

#include "market/core/IDataProvider.h"

namespace astock::market {

class SimProvider : public IDataProvider {
public:
    explicit SimProvider(const std::string& config);

    ProviderStatus get_status() const override;

    bool connect() override;
    void disconnect() override;

    void register_kline_callback(KLineCallback cb) override;
    void register_tick_callback(TickCallback cb) override;

    bool subscribe_kline(std::uint32_t symbol_id, std::uint16_t period) override;
    bool unsubscribe_kline(std::uint32_t symbol_id, std::uint16_t period) override;

    bool subscribe_tick(std::uint32_t symbol_id) override;
    bool unsubscribe_tick(std::uint32_t symbol_id) override;

    KLineBatch get_history_klines(
        std::uint32_t symbol_id,
        std::uint16_t period,
        std::uint64_t start_time,
        std::uint64_t end_time,
        std::size_t  limit) override;

private:
    mutable std::mutex   mutex_;
    std::string          config_;
    ProviderStatus       status_{ProviderStatus::DISCONNECTED};
    KLineCallback        kline_cb_{};
    TickCallback         tick_cb_{};

    double base_price_{100.0};
    std::uint64_t update_interval_ms_{1000};
};

} // namespace astock::market
