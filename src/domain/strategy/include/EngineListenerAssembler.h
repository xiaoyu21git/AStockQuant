#pragma once
// EngineListenerAssembler — 簿记挂钩一次性装配器 (P2, ADR-009①, 终审澄清 3.3)
// 委托模式: BookKeepingListener 持原监听器裸指针, onOrders 先 PositionBook.applyOrder
// 再转发原监听器; 外部已持有引用不受影响、无需卸载; onOrders 调用点零改动
// 装配点: StrategyEngine::setOrderListener (所有监听器注入的唯一切入点)

#include "IOrderListener.h"

#include <vector>

namespace domain::strategy {

class PositionBook;

/// @brief 簿记委托包装: 账本入账 (ADR-005) → 转发原订单监听器
/// 单实例单包装, install() 重复调用仅 rebind 原指针
class BookKeepingListener final : public IOrderListener {
public:
    explicit BookKeepingListener(PositionBook* book) : m_positionBook(book) {}

    /// @brief 重绑被委托的原始监听器 (可为空)
    void rebind(IOrderListener* raw) noexcept { m_raw = raw; }

    /// @brief 先簿记入账再转发 (提交日 = activeTradingDay, C3 豁免锚点)
    void onOrders(const std::vector<OrderRequest>& orders) override;

private:
    PositionBook* m_positionBook{nullptr};
    IOrderListener* m_raw{nullptr};
};

/// @brief 簿记挂钩一次性装配器 (ADR-009①)
class EngineListenerAssembler final {
public:
    explicit EngineListenerAssembler(PositionBook* book)
        : m_positionBook(book), m_wrapper(book) {}

    /// @brief 包装监听器并返回包装实例 (每次返回同一包装, 仅 rebind)
    IOrderListener* install(IOrderListener* raw);

private:
    PositionBook* m_positionBook{nullptr};
    BookKeepingListener m_wrapper;
};

} // namespace domain::strategy
