#pragma once

#include "market/core/IDataProvider.h"

namespace astock::market {

class ApiProvider : public IDataProvider {
public:
    explicit ApiProvider(const std::string& /*config*/) {}

    ProviderStatus get_status() const override { return ProviderStatus::DISCONNECTED; }

    bool connect() override { return true; }
    void disconnect() override {}

    void register_kline_callback(KLineCallback) override {}
    void register_tick_callback(TickCallback) override {}

    bool subscribe_kline(std::uint32_t, std::uint16_t) override { return true; }
    bool unsubscribe_kline(std::uint32_t, std::uint16_t) override { return true; }

    bool subscribe_tick(std::uint32_t) override { return true; }
    bool unsubscribe_tick(std::uint32_t) override { return true; }

    KLineBatch get_history_klines(
        std::uint32_t,
        std::uint16_t,
        std::uint64_t,
        std::uint64_t,
        std::size_t) override { return KLineBatch{}; }
};

} // namespace astock::market
