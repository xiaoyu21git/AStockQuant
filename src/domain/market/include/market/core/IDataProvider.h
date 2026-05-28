#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <functional>
#include <string>
#include <vector>

#include "market/core/DataTypes.h"

namespace astock::market {

enum class ProviderStatus {
    DISCONNECTED = 0,
    CONNECTING,
    CONNECTED,
    ERROR
};

class IDataProvider {
public:
    using KLineCallback = std::function<void(const KLine&)>;
    using TickCallback  = std::function<void(const TickData&)>;

    virtual ~IDataProvider() = default;

    virtual ProviderStatus get_status() const = 0;

    virtual bool connect() = 0;
    virtual void disconnect() = 0;

    virtual void register_kline_callback(KLineCallback cb) = 0;
    virtual void register_tick_callback(TickCallback cb) = 0;

    virtual bool subscribe_kline(std::uint32_t symbol_id, std::uint32_t period) = 0;
    virtual bool unsubscribe_kline(std::uint32_t symbol_id, std::uint32_t period) = 0;

    virtual bool subscribe_tick(std::uint32_t symbol_id) = 0;
    virtual bool unsubscribe_tick(std::uint32_t symbol_id) = 0;

    virtual KLineBatch get_history_klines(
        std::uint32_t symbol_id,
        std::uint32_t period,
        std::uint64_t start_time,
        std::uint64_t end_time,
        std::size_t  limit) = 0;

    virtual KLineBatch get_history_klines_batch(
        const std::vector<std::uint32_t>& symbol_ids,
        std::uint32_t period,
        std::uint64_t start_time,
        std::uint64_t end_time,
        std::size_t limit_per_symbol) = 0;
};

} // namespace astock::market
