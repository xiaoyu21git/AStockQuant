// OrderManager.cpp — 订单管理器实现，共享 GmSdkSingleton 的 ::Strategy
#include "OrderManager.h"
#include "../../../thirdparty/gmsdk/strategy.h"

namespace engine {

namespace {
std::string fromGm(const std::string& gm) {
    if (gm.compare(0, 5, "SHSE.") == 0) return gm.substr(5) + ".SH";
    if (gm.compare(0, 5, "SZSE.") == 0) return gm.substr(5) + ".SZ";
    if (gm.compare(0, 4, "BSE.")  == 0) return gm.substr(4) + ".BJ";
    return gm;
}

void extractOrders(::DataArray<Order>* arr, std::vector<OrderRecord>& out) {
    if (!arr || arr->status() != 0) {
        if (arr) arr->release();
        return;
    }
    for (size_t i = 0; i < arr->count(); ++i) {
        auto& o = arr->at(i);
        OrderRecord r;
        r.brokerOrderId = o.cl_ord_id;
        r.symbol        = fromGm(o.symbol);
        r.strategyId    = o.strategy_id;
        r.price         = static_cast<double>(o.price);
        r.quantity      = static_cast<int64_t>(o.volume);
        r.filledQty     = static_cast<int64_t>(o.filled_volume);
        r.filledVwap    = static_cast<double>(o.filled_vwap);
        r.side          = o.side;
        r.status        = o.status;
        out.push_back(r);
    }
    arr->release();
}
} // anonymous namespace

OrderManager& OrderManager::instance() {
    static OrderManager mgr;
    return mgr;
}

bool OrderManager::initialize(void* strategy) {
    if (!strategy) return false;
    m_strategy = strategy;
    return true;
}

void OrderManager::shutdown() { m_strategy = nullptr; }
bool OrderManager::initialized() const { return m_strategy != nullptr; }

std::vector<OrderRecord> OrderManager::queryOrders(const std::string& account) const {
    std::vector<OrderRecord> result;
    auto* s = static_cast<::Strategy*>(m_strategy);
    if (!s) return result;
    auto* arr = s->get_orders(account.empty() ? nullptr : account.c_str());
    extractOrders(arr, result);
    return result;
}

std::vector<OrderRecord> OrderManager::queryUnfinishedOrders(const std::string& account) const {
    std::vector<OrderRecord> result;
    auto* s = static_cast<::Strategy*>(m_strategy);
    if (!s) return result;
    auto* arr = s->get_unfinished_orders(account.empty() ? nullptr : account.c_str());
    extractOrders(arr, result);
    return result;
}

} // namespace engine
