#pragma once

#include "market/core/IDataProvider.h"
#include <memory>

namespace astock::market {

// 使用 MySQL 作为底层存储的数据源提供者
class DatabaseProvider : public IDataProvider {
public:
    explicit DatabaseProvider(const std::string& config);

    ProviderStatus get_status() const override;

    bool connect() override;
    void disconnect() override;

    void register_kline_callback(KLineCallback cb) override;
    void register_tick_callback(TickCallback cb) override;

    bool subscribe_kline(std::uint32_t symbol_id, std::uint32_t period) override;
    bool unsubscribe_kline(std::uint32_t symbol_id, std::uint32_t period) override;

    bool subscribe_tick(std::uint32_t symbol_id) override;
    bool unsubscribe_tick(std::uint32_t symbol_id) override;

    KLineBatch get_history_klines(
        std::uint32_t symbol_id,
        std::uint32_t period,
        std::uint64_t start_time,
        std::uint64_t end_time,
        std::size_t  limit) override;

    KLineBatch get_history_klines_batch(
        const std::vector<std::uint32_t>& symbol_ids,
        std::uint32_t period,
        std::uint64_t start_time,
        std::uint64_t end_time,
        std::size_t limit_per_symbol) override;

private:
    std::string   config_str_;
    std::string   host_{"localhost"};
    std::string   database_{"astock_quant"};
    std::string   username_{"root"};
    std::string   password_{};
    std::string   charset_{"utf8mb4"};
    std::uint16_t port_{3306};

    ProviderStatus status_{ProviderStatus::DISCONNECTED};

    KLineCallback kline_cb_{};
    TickCallback  tick_cb_{};

    void parse_config(const std::string& config_str);

    static bool is_daily_period(std::uint32_t period);
    static std::string period_to_timeframe(std::uint32_t period);
};

} // namespace astock::market
