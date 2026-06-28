import QtQuick 2.15
import AStock.Bridge 1.0 as Bridge
import "../../Trading" as Trading

Trading.RuntimeRuleObserverPanel {
    id: panel
    property var widgetConfig: ({})
    scaleFactor: Math.min(1.0, Math.max(0.4, height / 350))
    clip: true

    strategyService: Bridge.StrategyBridge
    highlightStrategyId: widgetConfig.strategyId || ""
    highlightStrategyName: widgetConfig.strategyName || ""
    runtimeRuleFeedLimit: Math.max(3, Math.floor(height / 80))
}
