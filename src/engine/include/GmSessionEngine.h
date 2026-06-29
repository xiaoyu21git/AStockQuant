// GmSessionEngine.h — 唯一 gmsdk 入口（engine 层，零 Qt）
// 所有 SDK 交互封装于此，上层只依赖本类提供的类型和回调。
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../domain/trading/TradingTypes.h"

namespace engine {

// ═══════════════════════════════════════════════════════════════════
// 数据类型 — 纯 C++，零 gmsdk 依赖
// ═══════════════════════════════════════════════════════════════════

struct GmTickData {
    std::string symbol;
    double price = 0, open = 0, high = 0, low = 0;
    double cumVolume = 0, cumAmount = 0, lastVolume = 0;
    int64_t tradingDay = 0;
    std::vector<double> bidPrices, bidVolumes, askPrices, askVolumes;
};

struct GmQuote {
    std::string symbol;
    double price = 0, open = 0, high = 0, low = 0;
    double preClose = 0, volume = 0;
    struct DepthLevel { double price = 0; double volume = 0; };
    std::vector<DepthLevel> bids, asks;
    bool valid = false;
    double limitPct() const;
    double changePct() const;
    bool   isLimitUp()   const;
    bool   isLimitDown() const;
};

// 统一定单类型 (定义在 domain::trading::OrderRequest)
using OrderRequest = domain::trading::OrderRequest;
using OrderSide    = domain::trading::OrderSide;
using OrderType    = domain::trading::OrderType;

struct OrderResult {
    std::string brokerOrderId;
    bool accepted = false;
    std::string message;
};

struct OrderUpdate {
    std::string brokerOrderId, symbol;
    double filledPrice = 0;
    int64_t filledQuantity = 0;
    enum Status { Submitted, PartialFilled, Filled, Cancelled, Rejected, Expired };
    Status status = Submitted;
    std::string message;
};

struct TradeFill {
    std::string fillId, brokerOrderId, symbol;
    double price = 0;
    int64_t quantity = 0;
    double commission = 0;
};

struct AccountInfo {
    std::string accountId;
    double totalAsset = 0, availableCash = 0, marketValue = 0, frozenCash = 0;
};

struct Position {
    std::string symbol;
    int64_t quantity = 0, availableQty = 0;
    double costPrice = 0, lastPrice = 0, marketValue = 0, unrealizedPnl = 0;
};

struct OrderRecord {
    std::string brokerOrderId, symbol, strategyId;
    double price = 0;
    int64_t quantity = 0, filledQty = 0;
    double filledVwap = 0;
    int side = 0, status = 0;
};

// ═══════════════════════════════════════════════════════════════════
// 单例
// ═══════════════════════════════════════════════════════════════════

class GmSessionEngine {
public:
    static GmSessionEngine& instance();

    // ── 生命周期 ──
    bool initialize(const std::string& token, const std::string& accountId);
    void shutdown();
    bool initialized() const;

    // ── 行情订阅 ──
    void subscribeTick(const std::string& symbol);
    void unsubscribeTick(const std::string& symbol);

    // ── 行情查询 ──
    std::optional<GmQuote> fetchQuote(const std::string& symbol);
    double fetchPreClose(const std::string& symbol);

    // ── 底层 Strategy 指针 ──
    void* strategy() const;

    // ── 符号转换 ──
    static std::string toGmSymbol(const std::string& internal);
    static std::string fromGmSymbol(const std::string& gm);

    // Impl — public，SessionStrategy 通过它访问回调
    struct Impl {
        std::atomic<bool> initialized{false};
        std::thread       strategyThread;
    };
    struct StrategyDeleter { void operator()(void*); };

    // 内部状态 — 由 SessionStrategy（匿名 namespace）直接访问
    std::unique_ptr<Impl> m_impl;
    std::unique_ptr<void, StrategyDeleter> m_strategy;
    std::mutex m_tickMutex;
    std::unordered_map<std::string, int> m_tickRefCount;

private:
    GmSessionEngine() = default;
    ~GmSessionEngine();
    GmSessionEngine(const GmSessionEngine&) = delete;
    GmSessionEngine& operator=(const GmSessionEngine&) = delete;

    std::unordered_map<std::string, double> m_preCloseCache;
    std::string m_cacheDate;
};

} // namespace engine
