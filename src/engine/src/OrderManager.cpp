// OrderManager.cpp — 订单管理器实现，共享 GmSessionEngine 的 ::Strategy
#include "OrderManager.h"
#include "GmSessionEngine.h"
#include "../../../thirdparty/gmsdk/strategy.h"

#include <mutex>

namespace engine {

namespace {

OrderRecord fromGmOrder(const Order& o) {
    OrderRecord r;
    r.brokerOrderId = o.cl_ord_id;
    r.symbol        = GmSessionEngine::fromGmSymbol(o.symbol);
    r.strategyId    = o.strategy_id;
    r.price         = static_cast<double>(o.price);
    r.quantity      = static_cast<int64_t>(o.volume);
    r.filledQty     = static_cast<int64_t>(o.filled_volume);
    r.filledVwap    = static_cast<double>(o.filled_vwap);
    r.side          = o.side;
    r.status        = o.status;
    return r;
}
} // anonymous namespace

OrderManager& OrderManager::instance() {
    static OrderManager mgr;
    return mgr;
}

bool OrderManager::initialize(::Strategy* strategy) {
    if (!strategy) return false;
    m_strategy = strategy;
    return true;
}

void OrderManager::shutdown() { m_strategy = nullptr; m_orders.clear(); }
bool OrderManager::initialized() const { return m_strategy != nullptr; }

std::vector<OrderRecord> OrderManager::queryOrders(const std::string& account) {
    auto* s = m_strategy;
    if (!s) return {};
    auto* arr = s->get_orders(account.empty() ? nullptr : account.c_str());
    if (!arr || arr->status() != 0) { if (arr) arr->release(); return {}; }
    for (size_t i = 0; i < arr->count(); ++i)
        m_orders[arr->at(i).cl_ord_id] = fromGmOrder(arr->at(i));
    arr->release();
    std::vector<OrderRecord> result;
    for (auto& [id, o] : m_orders) result.push_back(o);
    return result;
}

std::vector<OrderRecord> OrderManager::queryUnfinishedOrders(const std::string& account) {
    auto* s = m_strategy;
    if (!s) return {};
    auto* arr = s->get_unfinished_orders(account.empty() ? nullptr : account.c_str());
    if (!arr || arr->status() != 0) { if (arr) arr->release(); return {}; }
    std::vector<OrderRecord> result;
    for (size_t i = 0; i < arr->count(); ++i) {
        auto r = fromGmOrder(arr->at(i));
        m_orders[r.brokerOrderId] = r;
        result.push_back(r);
    }
    arr->release();
    return result;
}

std::vector<OrderRecord> OrderManager::syncOrders(const std::string& account) {
    auto* s = m_strategy;
    if (!s) return {};
    auto* arr = s->get_orders(account.empty() ? nullptr : account.c_str());
    if (!arr || arr->status() != 0) { if (arr) arr->release(); return {}; }

    std::vector<OrderRecord> changed;
    for (size_t i = 0; i < arr->count(); ++i) {
        auto r = fromGmOrder(arr->at(i));
        auto it = m_orders.find(r.brokerOrderId);
        if (it == m_orders.end() || it->second.status != r.status
            || it->second.filledQty != r.filledQty) {
            changed.push_back(r);
        }
        m_orders[r.brokerOrderId] = r;
    }
    arr->release();
    return changed;
}

} // namespace engine
