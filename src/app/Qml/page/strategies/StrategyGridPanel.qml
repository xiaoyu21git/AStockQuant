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
    Layout.preferredHeight: 720
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
                text: "策略列表"
                font.pixelSize: panelRoot.page.fontSizeLarge
                font.weight: Font.DemiBold
                color: panelRoot.page.textPrimary
            }

            Text {
                Layout.fillWidth: true
                text: "双列卡片展示当前可用策略，点击卡片即可同步下方详情与控制区域。"
                font.pixelSize: 12
                color: panelRoot.page.textSecondary
                wrapMode: Text.WordWrap
            }
        }

        GridView {
            id: strategyGridView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: strategyVisibleModel
            cellWidth: (width - 30) / 2
            cellHeight: 280

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AlwaysOff
            }

            delegate: Components.StrategyCard {
                property int sourceIndex: model.sourceIndex
                property var sourceStrategy: panelRoot.page.getStrategyData(sourceIndex)

                width: strategyGridView.cellWidth - 12
                height: strategyGridView.cellHeight - 20
                strategyId: sourceStrategy ? (sourceStrategy.strategyId || "") : ""
                strategyName: sourceStrategy ? (sourceStrategy.strategyName || sourceStrategy.name || "未命名策略") : "未命名策略"
                displayName: sourceStrategy ? (sourceStrategy.strategyName || sourceStrategy.name || "未命名策略") : "未命名策略"
                description: sourceStrategy ? panelRoot.page.buildStrategyCardDescription(sourceStrategy) : "暂无描述"
                status: {
                    var s = panelRoot.page.getStrategyData(sourceIndex)
                    return (s && s.displayStatus) || "已停止"
                }
                tags: sourceStrategy ? panelRoot.page.buildStrategyRuntimeTags(sourceStrategy) : []
                startActionAvailable: sourceStrategy ? panelRoot.page.getStrategyStartGateState(sourceStrategy).canStart : false
                startActionLabel: sourceStrategy ? panelRoot.page.getStrategyStartActionLabel(sourceStrategy) : "启动实盘"
                startActionHint: sourceStrategy ? panelRoot.page.getStrategyStartActionHint(sourceStrategy) : ""
                returns: sourceStrategy ? (parseFloat(sourceStrategy.returns) || 0.0) : 0.0
                sharpeRatio: sourceStrategy ? (parseFloat(sourceStrategy.sharpeRatio) || 0.0) : 0.0
                maxDrawdown: sourceStrategy ? (parseFloat(sourceStrategy.maxDrawdown) || 0.0) : 0.0
                winRate: sourceStrategy ? (parseFloat(sourceStrategy.winRate) || 0.0) : 0.0
                runningDays: sourceStrategy ? (sourceStrategy.runningDays || 0) : 0
                tradesCount: sourceStrategy ? (sourceStrategy.tradesCount || 0) : 0
                dailyPnL: sourceStrategy ? (parseFloat(sourceStrategy.dailyPnL) || 0) : 0
                position: sourceStrategy ? (parseFloat(sourceStrategy.position) || 0) : 0
                selected: panelRoot.page.selectedStrategyId !== ""
                    ? panelRoot.page.selectedStrategyId === (sourceStrategy ? (sourceStrategy.strategyId || "") : "")
                    : panelRoot.page.selectedStrategyIndex === sourceIndex
                showMiniChart: true
                showParameterPanel: false
                cardWidth: strategyGridView.cellWidth - 12
                cardHeight: 260
                enableCardClick: true

                onClicked: {
                    panelRoot.page.selectStrategyAt(sourceIndex)
                }

                onEntitySelected: function(entityId) {
                    panelRoot.page.selectStrategyAt(sourceIndex)
                }

                onStartClicked: {
                    panelRoot.page.startStrategyFromCard(sourceStrategy)
                }

                onStartActionHintClicked: {
                    panelRoot.page.handleStrategyStartActionHint(sourceStrategy)
                }

                onPauseClicked: {
                    panelRoot.page.stopStrategyFromCard(sourceStrategy)
                }

                onStopClicked: {
                    panelRoot.page.stopStrategyFromCard(sourceStrategy)
                }

                onOptimizeClicked: {
                    panelRoot.page.optimizeStrategy()
                }

                onEditClicked: {
                    panelRoot.page.openStrategyCreation(sourceStrategy || ({}))
                }

                onDeleteClicked: {
                    panelRoot.page.requestDeleteStrategy(
                        sourceStrategy ? (sourceStrategy.strategyId || "") : "",
                        sourceStrategy ? (sourceStrategy.strategyName || sourceStrategy.name || "未命名策略") : "未命名策略"
                    )
                }
            }
        }
    }
}
