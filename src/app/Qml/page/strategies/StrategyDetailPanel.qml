import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../components" as Components
import "../../components/Strategy" as StrategyComponents
import "../../components/Base" as BaseComponents

Rectangle {
    id: panelRoot
    // -- Interface --
    required property QtObject page
    visible: !panelRoot.page.showBacktestWorkbench && !panelRoot.page.showPerformance
    Layout.fillWidth: true
    Layout.preferredHeight: panelRoot.page.hasSelectedStrategy ? 360 : 200
    Layout.alignment: Qt.AlignHCenter
    radius: panelRoot.page.borderRadiusXLarge
    color: panelRoot.page.secondaryBg
    border.color: panelRoot.page.sectionCardBorderColor

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: panelRoot.page.sectionCardPadding
        spacing: panelRoot.page.sectionCardSpacing

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            Text {
                text: "策略详情与控制"
                font.pixelSize: panelRoot.page.fontSizeLarge
                font.weight: Font.DemiBold
                color: panelRoot.page.textPrimary
            }

            Text {
                Layout.fillWidth: true
                text: "围绕当前选中策略集中展示详情、运行入口和主要控制动作。"
                font.pixelSize: 12
                color: panelRoot.page.textSecondary
                wrapMode: Text.WordWrap
            }
        }

        Text {
            text: "请从上方策略列表中选择一个策略"
            font.pixelSize: panelRoot.page.fontSizeNormal
            color: panelRoot.page.textTertiary
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            visible: !panelRoot.page.hasSelectedStrategy
        }

        Components.StrategyCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panelRoot.page.hasSelectedStrategy

            property var selectedStrategy: { panelRoot.page.statusRefreshCounter; return panelRoot.page.getSelectedStrategySummary() }

            strategyId: selectedStrategy ? (selectedStrategy.strategyId || "") : ""
            strategyName: selectedStrategy ? (selectedStrategy.strategyName || selectedStrategy.name || "未命名策略") : ""
            displayName: selectedStrategy ? (selectedStrategy.strategyName || selectedStrategy.name || "未命名策略") : ""
            strategyType: selectedStrategy ? (selectedStrategy.strategyType || "趋势策略") : "趋势策略"
            description: selectedStrategy ? panelRoot.page.buildStrategyCardDescription(selectedStrategy) : "暂无描述"
            status: {
                panelRoot.page.statusRefreshCounter
                var sid = panelRoot.page.selectedStrategyId
                var local = panelRoot.page.localStatusOverrides[sid] || ""
                if (local) return local
                var s = panelRoot.page.getSelectedStrategySummary()
                return (s && s.displayStatus) || "已停止"
            }
            tags: selectedStrategy ? panelRoot.page.buildStrategyRuntimeTags(selectedStrategy) : []
            startActionAvailable: selectedStrategy ? panelRoot.page.getStrategyStartGateState(selectedStrategy).canStart : false
            startActionLabel: selectedStrategy ? panelRoot.page.getStrategyStartActionLabel(selectedStrategy) : "启动实盘"
            startActionHint: selectedStrategy ? panelRoot.page.getStrategyStartActionHint(selectedStrategy) : ""
            returns: selectedStrategy ? parseFloat(selectedStrategy.returns) || 0.0 : 0.0
            sharpeRatio: selectedStrategy ? parseFloat(selectedStrategy.sharpeRatio) || 0.0 : 0.0
            maxDrawdown: selectedStrategy ? parseFloat(selectedStrategy.maxDrawdown) || 0.0 : 0.0
            winRate: selectedStrategy ? parseFloat(selectedStrategy.winRate) || 0.0 : 0.0
            runningDays: selectedStrategy ? (selectedStrategy.runningDays || 0) : 0
            tradesCount: selectedStrategy ? (selectedStrategy.tradesCount || 0) : 0
            dailyPnL: selectedStrategy ? parseFloat(selectedStrategy.dailyPnL) || 0 : 0
            position: selectedStrategy ? parseFloat(selectedStrategy.position) || 0 : 0
            selected: true
            showMiniChart: true
            showParameterPanel: true
            cardWidth: parent.width - 32
            cardHeight: parent.height - 32

            onStartClicked: {
                panelRoot.page.startStrategyFromCard(selectedStrategy)
            }

            onStartActionHintClicked: {
                panelRoot.page.handleStrategyStartActionHint(selectedStrategy)
            }

            onPauseClicked: {
                panelRoot.page.stopStrategyFromCard(selectedStrategy)
            }

            onStopClicked: {
                panelRoot.page.stopStrategyFromCard(selectedStrategy)
            }

            onOptimizeClicked: {
                panelRoot.page.optimizeStrategy()
            }

            onEditClicked: {
                panelRoot.page.openStrategyCreation(selectedStrategy || ({}))
            }

            onDeleteClicked: {
                panelRoot.page.requestDeleteStrategy(
                    selectedStrategy ? (selectedStrategy.strategyId || "") : "",
                    selectedStrategy ? (selectedStrategy.strategyName || selectedStrategy.name || "未命名策略") : "未命名策略"
                )
            }
        }
    }
}
