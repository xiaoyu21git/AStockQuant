// src/market/core/MarketData.cpp
#include "core/MarketData.h"
#include "providers/SimProvider.h"
#include <chrono>
#include <algorithm>
#include <iostream>

namespace domain {
namespace market {

// ============== MarketDataManager 实现 ==============

MarketDataManager::~MarketDataManager() {
    if (data_provider_) {
        data_provider_->disconnect();
    }
}

bool MarketDataManager::initialize(DataProviderFactory::ProviderType type,
                                  const std::string& config) {
    data_provider_ = DataProviderFactory::create_provider(type, config);
    if (!data_provider_) {
        return false;
    }
    
    // 注册内部回调
    data_provider_->register_kline_callback(
        [this](const KLine& kline) { on_kline_received(kline); });
    
    data_provider_->register_tick_callback(
        [this](const TickData& tick) { on_tick_received(tick); });
    
    return data_provider_->connect();
}

bool MarketDataManager::subscribe_kline(uint32_t symbol_id, uint16_t period) {
    if (!data_provider_) return false;
    
    // 先添加到订阅管理器
    if (!subscription_manager_.add_subscription(symbol_id, period)) {
        return false;
    }
    
    // 再通知数据源
    return data_provider_->subscribe_kline(symbol_id, period);
}

bool MarketDataManager::subscribe_tick(uint32_t symbol_id) {
    if (!data_provider_) return false;
    return data_provider_->subscribe_tick(symbol_id);
}

bool MarketDataManager::unsubscribe_kline(uint32_t symbol_id, uint16_t period) {
    if (!data_provider_) return false;
    
    if (!subscription_manager_.remove_subscription(symbol_id, period)) {
        return false;
    }
    
    return data_provider_->unsubscribe_kline(symbol_id, period);
}

bool MarketDataManager::unsubscribe_tick(uint32_t symbol_id) {
    if (!data_provider_) return false;
    return data_provider_->unsubscribe_tick(symbol_id);
}

KLineBatch MarketDataManager::get_history_klines(
    uint32_t symbol_id,
    uint16_t period,
    uint64_t start_time,
    uint64_t end_time,
    size_t limit) {
    
    auto start = std::chrono::high_resolution_clock::now();
    
    if (!data_provider_) {
        return KLineBatch();
    }
    
    auto result = data_provider_->get_history_klines(
        symbol_id, period, start_time, end_time, limit);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double, std::milli>(end - start);
    
    // 更新统计
    stats_.avg_query_time_ms = (stats_.avg_query_time_ms * stats_.kline_count + 
                               duration.count()) / (stats_.kline_count + 1);
    stats_.kline_count += result.size();
    
    return result;
}

std::optional<KLine> MarketDataManager::get_latest_kline(uint32_t symbol_id, uint16_t period) {
    return data_cache_.get_kline(symbol_id, period);
}

std::optional<TickData> MarketDataManager::get_latest_tick(uint32_t symbol_id) {
    return data_cache_.get_tick(symbol_id);
}

std::vector<KLine> MarketDataManager::get_latest_klines(
    const std::vector<uint32_t>& symbol_ids,
    uint16_t period) {
    
    std::vector<KLine> result;
    result.reserve(symbol_ids.size());
    
    for (uint32_t symbol_id : symbol_ids) {
        if (auto kline = get_latest_kline(symbol_id, period)) {
            result.push_back(*kline);
        }
    }
    
    return result;
}

MarketDataManager::CallbackId MarketDataManager::register_kline_callback(KLineCallback callback) {
    return callback_dispatcher_.register_callback(std::move(callback));
}

MarketDataManager::CallbackId MarketDataManager::register_tick_callback(TickCallback callback) {
    return callback_dispatcher_.register_callback(std::move(callback));
}

void MarketDataManager::unregister_callback(CallbackId id) {
    callback_dispatcher_.unregister_callback(id);
}

MarketDataManager::Statistics MarketDataManager::get_statistics() const {
    auto stats = stats_;
    auto subscriptions = subscription_manager_.get_subscriptions();
    
    // 更新缓存命中率等实时数据
    // 这里可以添加更多统计信息
    
    return stats;
}

void MarketDataManager::on_kline_received(const KLine& kline) {
    // 更新缓存
    data_cache_.update_kline(kline.symbol_id, kline.period, kline);
    
    // 分发给回调
    callback_dispatcher_.dispatch_kline(kline);
    
    // 更新统计
    stats_.kline_count++;
}

void MarketDataManager::on_tick_received(const TickData& tick) {
    // 更新缓存
    data_cache_.update_tick(tick.symbol_id, tick);
    
    // 分发给回调
    callback_dispatcher_.dispatch_tick(tick);
    
    // 更新统计
    stats_.tick_count++;
}

// ============== SubscriptionManager 实现 ==============

bool MarketDataManager::SubscriptionManager::add_subscription(uint32_t symbol_id, uint16_t period) {
    std::unique_lock lock(mutex_);
    uint64_t key = (static_cast<uint64_t>(symbol_id) << 16) | period;
    
    auto it = subscriptions_.find(key);
    if (it != subscriptions_.end()) {
        it->second.ref_count++;
        return true;
    }
    
    Subscription sub;
    sub.symbol_id = symbol_id;
    sub.period = period;
    sub.ref_count = 1;
    
    subscriptions_[key] = sub;
    return true;
}

bool MarketDataManager::SubscriptionManager::remove_subscription(uint32_t symbol_id, uint16_t period) {
    std::unique_lock lock(mutex_);
    uint64_t key = (static_cast<uint64_t>(symbol_id) << 16) | period;
    
    auto it = subscriptions_.find(key);
    if (it == subscriptions_.end()) {
        return false;
    }
    
    if (--it->second.ref_count == 0) {
        subscriptions_.erase(it);
    }
    
    return true;
}

std::vector<MarketDataManager::Subscription> 
MarketDataManager::SubscriptionManager::get_subscriptions() const {
    std::shared_lock lock(mutex_);
    std::vector<Subscription> result;
    result.reserve(subscriptions_.size());
    
    for (const auto& pair : subscriptions_) {
        result.push_back(pair.second);
    }
    
    return result;
}

// ============== LatestDataCache 实现 ==============

void MarketDataManager::LatestDataCache::update_kline(uint32_t symbol_id, uint16_t period, const KLine& kline) {
    std::unique_lock lock(mutex_);
    uint64_t key = (static_cast<uint64_t>(symbol_id) << 16) | period;
    
    auto& atomic_kline = kline_cache_[key];
    atomic_kline.timestamp.store(kline.timestamp, std::memory_order_relaxed);
    atomic_kline.close.store(kline.close, std::memory_order_relaxed);
    atomic_kline.data = kline;
}

void MarketDataManager::LatestDataCache::update_tick(uint32_t symbol_id, const TickData& tick) {
    std::unique_lock lock(mutex_);
    tick_cache_[symbol_id] = tick;
}

std::optional<KLine> MarketDataManager::LatestDataCache::get_kline(uint32_t symbol_id, uint16_t period) const {
    std::shared_lock lock(mutex_);
    uint64_t key = (static_cast<uint64_t>(symbol_id) << 16) | period;
    
    auto it = kline_cache_.find(key);
    if (it != kline_cache_.end()) {
        return it->second.data;
    }
    
    return std::nullopt;
}

std::optional<TickData> MarketDataManager::LatestDataCache::get_tick(uint32_t symbol_id) const {
    std::shared_lock lock(mutex_);
    auto it = tick_cache_.find(symbol_id);
    if (it != tick_cache_.end()) {
        return it->second;
    }
    
    return std::nullopt;
}

// ============== CallbackDispatcher 实现 ==============

MarketDataManager::CallbackId MarketDataManager::CallbackDispatcher::register_callback(KLineCallback callback) {
    std::unique_lock lock(mutex_);
    CallbackId id = next_id_++;
    callbacks_[id] = CallbackEntry{id, std::move(callback)};
    return id;
}

MarketDataManager::CallbackId MarketDataManager::CallbackDispatcher::register_callback(TickCallback callback) {
    std::unique_lock lock(mutex_);
    CallbackId id = next_id_++;
    callbacks_[id] = CallbackEntry{id, std::move(callback)};
    return id;
}

void MarketDataManager::CallbackDispatcher::unregister_callback(CallbackId id) {
    std::unique_lock lock(mutex_);
    callbacks_.erase(id);
}

void MarketDataManager::CallbackDispatcher::dispatch_kline(const KLine& kline) {
    std::shared_lock lock(mutex_);
    
    for (const auto& pair : callbacks_) {
        if (auto* callback = std::get_if<KLineCallback>(&pair.second.callback)) {
            try {
                (*callback)(kline);
            } catch (const std::exception& e) {
                // 记录错误但继续处理其他回调
                std::cerr << "Error in kline callback: " << e.what() << std::endl;
            }
        }
    }
}

void MarketDataManager::CallbackDispatcher::dispatch_tick(const TickData& tick) {
    std::shared_lock lock(mutex_);
    
    for (const auto& pair : callbacks_) {
        if (auto* callback = std::get_if<TickCallback>(&pair.second.callback)) {
            try {
                (*callback)(tick);
            } catch (const std::exception& e) {
                std::cerr << "Error in tick callback: " << e.what() << std::endl;
            }
        }
    }
}

} // namespace market
} // namespace astock