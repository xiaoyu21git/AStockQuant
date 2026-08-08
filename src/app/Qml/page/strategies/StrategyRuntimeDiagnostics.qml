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
    visible: !panelRoot.page.showBacktestWorkbench && !panelRoot.page.showPerformance && panelRoot.page.hasSelectedStrategy
    Layout.fillWidth: true
    Layout.preferredHeight: runtimeDiagnosticSection.issueText.length > 0 ? 312 : 252
    Layout.alignment: Qt.AlignHCenter
    radius: panelRoot.page.borderRadiusXLarge
    color: panelRoot.page.secondaryBg
    border.color: panelRoot.page.sectionCardBorderColor

    ColumnLayout {
        id: runtimeDiagnosticSection
        anchors.fill: parent
        anchors.margins: panelRoot.page.sectionCardPadding
        spacing: panelRoot.page.sectionCardSpacing

        property var selectedStrategySummary: panelRoot.page.getSelectedStrategySummary()
        property var tradingConfiguration: panelRoot.page.currentTradingConfiguration()
        property var marketCalendarSnapshot: panelRoot.page.currentMarketCalendarSnapshot()
        property string selectedStrategyId: selectedStrategySummary
            ? (selectedStrategySummary.strategyId || "")
            : ""
        property var runtimeSnapshot: panelRoot.page.currentRuntimeSnapshot(selectedStrategySummary)
        property bool hasRuntimeSnapshot: panelRoot.page.hasRuntimeSnapshotData(runtimeSnapshot)
        property bool isBoundStrategy: panelRoot.page.selectedStrategyId !== ""
            && panelRoot.page.isStrategyBoundToTradingConfiguration(selectedStrategySummary, tradingConfiguration)
        property string displayStatus: selectedStrategySummary
            ? panelRoot.page.getStrategyDisplayStatus(selectedStrategySummary)
            : "STOPPED"
        property string issueTitle: hasRuntimeSnapshot ? "最近错误" : "日历回退原因"
        property string issueText: hasRuntimeSnapshot
            ? panelRoot.page.normalizeRuntimeDisplayValue(runtimeSnapshot.lastError, "")
            : (isBoundStrategy
                ? panelRoot.page.normalizeRuntimeDisplayValue(marketCalendarSnapshot.error, "")
                : "")
        property var diagnosticItems: [
            {
                label: "显示状态",
                value: hasRuntimeSnapshot
                    ? panelRoot.page.normalizeRuntimeDisplayValue(runtimeSnapshot.stateLabel, panelRoot.page.getStrategyDisplayStatusLabel(displayStatus))
                    : panelRoot.page.getStrategyDisplayStatusLabel(displayStatus),
                accent: panelRoot.page.getRuntimeDiagnosticColor(displayStatus)
            },
            {
                label: "交易绑定",
                value: panelRoot.page.describeStrategyBinding(selectedStrategySummary, tradingConfiguration),
                accent: isBoundStrategy ? accentBlue : panelRoot.page.textSecondary
            },
            {
                label: "订阅同步",
                value: panelRoot.page.getStrategySubscriptionSyncLabel(selectedStrategySummary, tradingConfiguration),
                accent: panelRoot.page.getStrategySubscriptionSyncAccent(selectedStrategySummary, tradingConfiguration)
            },
            {
                label: "日历来源",
                value: panelRoot.page.normalizeRuntimeDisplayValue(marketCalendarSnapshot.sourceLabel, "本地时间窗"),
                accent: marketCalendarSnapshot.holidayAware ? successGreen : panelRoot.page.warningAmber
            },
            {
                label: "日历阶段",
                value: panelRoot.page.getMarketCalendarPhaseLabel(marketCalendarSnapshot),
                accent: panelRoot.page.getMarketCalendarStatusAccent(marketCalendarSnapshot)
            },
            {
                label: "账户 ID",
                value: panelRoot.page.normalizeRuntimeDisplayValue(
                    hasRuntimeSnapshot ? runtimeSnapshot.accountId : (isBoundStrategy ? tradingConfiguration.accountId : "")),
                accent: panelRoot.page.textPrimary
            },
            {
                label: "策略名称",
                value: selectedStrategySummary ? (selectedStrategySummary.strategyName || selectedStrategySummary.name || "--") : "--",
                accent: panelRoot.page.textPrimary
            },
            {
                label: "最近收盘交易日",
                value: panelRoot.page.normalizeRuntimeDisplayValue(marketCalendarSnapshot.latestClosedTradeDate, "--"),
                accent: panelRoot.page.textPrimary
            },
            {
                label: "连接状态",
                value: panelRoot.page.formatRuntimeBooleanValue(
                    hasRuntimeSnapshot ? runtimeSnapshot.connected : undefined,
                    "已连接",
                    "未连接",
                    "--"),
                accent: hasRuntimeSnapshot && runtimeSnapshot.connected ? successGreen : panelRoot.page.textSecondary
            },
            {
                label: "初始化",
                value: panelRoot.page.formatRuntimeBooleanValue(
                    hasRuntimeSnapshot ? runtimeSnapshot.initialized : undefined,
                    "已初始化",
                    "未初始化",
                    "--"),
                accent: hasRuntimeSnapshot && runtimeSnapshot.initialized ? successGreen : panelRoot.page.textSecondary
            }
        ]

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            Text {
                text: "运行时诊断"
                font.pixelSize: panelRoot.page.fontSizeLarge
                font.weight: Font.DemiBold
                color: panelRoot.page.textPrimary
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: panelRoot.page.textSecondary
                text: runtimeDiagnosticSection.hasRuntimeSnapshot
                    ? "当前状态来自真实运行时会话快照，可直接用于判断策略是否已经进入交易运行态。"
                    : (runtimeDiagnosticSection.isBoundStrategy
                        ? "当前策略已绑定到活动交易配置，但暂未发现运行时会话，页面会优先参考交易日历，再回退到本地时间窗。"
                        : "当前策略尚未绑定到活动交易配置，因此不会出现对应的运行时会话。")
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 10
            rowSpacing: 8

            Repeater {
                model: runtimeDiagnosticSection.diagnosticItems

                delegate: Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    radius: 8
                    color: "#0B1220"
                    border.width: 1
                    border.color: Qt.rgba(71 / 255, 85 / 255, 105 / 255, 0.35)

                    Column {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 3

                        Text {
                            text: modelData.label
                            font.pixelSize: 11
                            color: panelRoot.page.textTertiary
                        }

                        Text {
                            text: modelData.value
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            color: modelData.accent || panelRoot.page.textPrimary
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: runtimeDiagnosticSection.issueText.length > 0 ? errorText.implicitHeight + 24 : 0
            visible: runtimeDiagnosticSection.issueText.length > 0
            radius: 8
            color: Qt.rgba(239 / 255, 68 / 255, 68 / 255, 0.10)
            border.width: 1
            border.color: Qt.rgba(239 / 255, 68 / 255, 68 / 255, 0.35)

            Column {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4

                Text {
                    text: runtimeDiagnosticSection.issueTitle
                    font.pixelSize: 11
                    font.weight: Font.Medium
                    color: panelRoot.page.riseRed
                }

                Text {
                    id: errorText
                    width: parent.width
                    text: runtimeDiagnosticSection.issueText
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: panelRoot.page.textPrimary
                }
            }
        }
    }
}
