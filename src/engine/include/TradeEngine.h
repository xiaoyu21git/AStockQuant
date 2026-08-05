// TradeEngine.h — 交易引擎（engine 层，零 Qt）
// 哑管道：接单 → gmsdk。不判断，不风控，不查账户，不管订单对账。
// 新增: 自动撤单 + 拆单
#pragma once

#include "GmSessionEngine.h"
#include "foundation/Utils/Uuid.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace engine {

class TradeEngine {
public:
    static TradeEngine& instance();

    bool initialize(void* strategy);
    void shutdown();
    bool initialized() const;

    // ── 下单 ──
    OrderResult submitOrder(const OrderRequest& req);
    std::vector<OrderResult> submitBatch(const std::vector<OrderRequest>& reqs);

    // ── 拆单 — 大单拆小单, 按时间间隔分批提交 ──
    struct SplitSpec {
        int chunkSize{1000};   // 每批股数
        int intervalMs{500};   // 批次间隔毫秒
    };
    std::vector<OrderResult> submitSplit(const OrderRequest& req, SplitSpec spec);

    // ── 撤单 — 按客户端 clOrdId 撤销指定订单 ──
    bool cancelOrder(const std::string& clOrdId);

    // ── 自动撤单 — 超时未成交自动撤 ──
    void setAutoCancelTimeout(std::chrono::milliseconds timeout);
    void startAutoCancel();
    void stopAutoCancel();

    // ── 回调 ──
    using OrderUpdateFn = std::function<void(const OrderUpdate&)>;
    using TradeFillFn   = std::function<void(const TradeFill&)>;
    void setOnOrderUpdate(OrderUpdateFn cb);
    void setOnTradeFill(TradeFillFn cb);
    void onOrderStatus(const OrderUpdate& u);
    void onTradeFill(const TradeFill& f);

private:
    TradeEngine() = default;
    ~TradeEngine() { stopAutoCancel(); }

    struct OrderRecord {
        std::string clOrdId;       // 客户端ID
        std::string brokerOrderId; // gmsdk返回的broker ID（用于撤单）
        std::chrono::steady_clock::time_point submitTime;
        bool filled{false};
    };
    mutable std::mutex m_ordersMutex;
    std::unordered_map<std::string, OrderRecord> m_activeOrders; // key=clOrdId

    void* m_strategy = nullptr;
    foundation::utils::Uuid m_orderSub;
    foundation::utils::Uuid m_fillSub;
    OrderUpdateFn m_onOrderUpdate;
    TradeFillFn   m_onTradeFill;
    std::chrono::milliseconds m_autoCancelTimeout{0};
    std::unique_ptr<std::thread> m_autoCancelThread;
    std::atomic<bool> m_autoCancelRunning{false};
};

} // namespace engine
