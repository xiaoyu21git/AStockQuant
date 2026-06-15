#pragma once
// ═════════════════════════════════════════════════════════════════════════
// UiLifecycleCoordinator — UI 生命周期协调器
// 负责延迟初始化阶段管理：交易页面和策略库页面的两阶段懒加载
// ═════════════════════════════════════════════════════════════════════════

#include <QObject>

namespace bridge {

class UiLifecycleCoordinator : public QObject {
    Q_OBJECT
public:
    explicit UiLifecycleCoordinator(QObject* parent = nullptr);

    /// @brief 激活交易页面 — 触发 TradingPage 的两阶段延迟初始化
    Q_INVOKABLE void activateTradingPage();

    /// @brief 激活策略库页面 — 触发 StrategyLibraryPage 的服务初始化
    Q_INVOKABLE void activateStrategyLibraryPage();

signals:
    void tradingPageActivated();
    void strategyLibraryPageActivated();

private:
    bool m_tradingPageActivated{false};
    bool m_strategyLibraryPageActivated{false};
};

} // namespace bridge
