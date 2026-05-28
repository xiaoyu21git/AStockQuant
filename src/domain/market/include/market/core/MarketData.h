#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <variant>
#include <vector>

#include "market/core/DataTypes.h"
#include "market/core/IDataProvider.h"

namespace astock::market {

class DataProviderFactory {
public:
    enum class ProviderType {
        FILE,
        DATABASE,
        API,
        SIMULATED
    };

    static std::shared_ptr<IDataProvider> create_provider(
        ProviderType type,
        const std::string& config);
};

class MarketDataManager {
public:
    using KLineCallback = std::function<void(const KLine&)>;
    using TickCallback  = std::function<void(const TickData&)>;
    using CallbackId    = std::uint64_t;

    struct Statistics {
        std::size_t kline_count{0};
        std::size_t tick_count{0};
        double      avg_query_time_ms{0.0};
    };

    static MarketDataManager& instance();

    ~MarketDataManager();

    bool initialize(DataProviderFactory::ProviderType type,
                    const std::string& config);

    bool subscribe_kline(std::uint32_t symbol_id, std::uint32_t period);
    bool subscribe_tick(std::uint32_t symbol_id);
    bool unsubscribe_kline(std::uint32_t symbol_id, std::uint32_t period);
    bool unsubscribe_tick(std::uint32_t symbol_id);

    KLineBatch get_history_klines(
        std::uint32_t symbol_id,
        std::uint32_t period,
        std::uint64_t start_time,
        std::uint64_t end_time,
        std::size_t  limit);

    std::optional<KLine>    get_latest_kline(std::uint32_t symbol_id, std::uint32_t period);
    std::optional<TickData> get_latest_tick(std::uint32_t symbol_id);

    std::vector<KLine> get_latest_klines(
        const std::vector<std::uint32_t>& symbol_ids,
        std::uint32_t period);

    CallbackId register_kline_callback(KLineCallback callback);
    CallbackId register_tick_callback(TickCallback callback);
    void       unregister_callback(CallbackId id);

    Statistics get_statistics() const;

private:
    MarketDataManager() = default;
    MarketDataManager(const MarketDataManager&) = delete;
    MarketDataManager& operator=(const MarketDataManager&) = delete;

    // ======== 订阅管理 ========
    struct Subscription {
        std::uint32_t symbol_id{0};
        std::uint32_t period{0};
        std::size_t   ref_count{0};
    };

    class SubscriptionManager {
    public:
        bool add_subscription(std::uint32_t symbol_id, std::uint32_t period);
        bool remove_subscription(std::uint32_t symbol_id, std::uint32_t period);
        std::vector<Subscription> get_subscriptions() const;

    private:
        mutable std::shared_mutex                                  mutex_;
        std::unordered_map<std::uint64_t, Subscription> subscriptions_;
    };

    // ======== 最新数据缓存 ========
    class LatestDataCache {
    public:
        struct AtomicKLine {
            std::atomic<std::uint64_t> timestamp{0};
            std::atomic<double>        close{0.0};
            KLine                      data{};
        };

        void update_kline(std::uint32_t symbol_id, std::uint32_t period, const KLine& kline);
        void update_tick(std::uint32_t symbol_id, const TickData& tick);

        std::optional<KLine>    get_kline(std::uint32_t symbol_id, std::uint32_t period) const;
        std::optional<TickData> get_tick(std::uint32_t symbol_id) const;

    private:
        mutable std::shared_mutex                                       mutex_;
        std::unordered_map<std::uint64_t, AtomicKLine> kline_cache_;
        std::unordered_map<std::uint32_t, TickData>    tick_cache_;
    };

    // ======== 回调分发 ========
    class CallbackDispatcher {
    public:
        CallbackId register_callback(KLineCallback callback);
        CallbackId register_callback(TickCallback callback);
        void       unregister_callback(CallbackId id);

        void dispatch_kline(const KLine& kline);
        void dispatch_tick(const TickData& tick);

    private:
        struct CallbackEntry {
            CallbackId                                            id;
            std::variant<KLineCallback, TickCallback> callback;
        };

        mutable std::shared_mutex                                    mutex_;
        std::unordered_map<CallbackId, CallbackEntry> callbacks_;
        CallbackId                                                  next_id_{1};
    };

    void on_kline_received(const KLine& kline);
    void on_tick_received(const TickData& tick);

    std::shared_ptr<IDataProvider> data_provider_;
    SubscriptionManager            subscription_manager_;
    LatestDataCache                data_cache_;
    CallbackDispatcher             callback_dispatcher_;
    Statistics                     stats_;
};

} // namespace astock::market
