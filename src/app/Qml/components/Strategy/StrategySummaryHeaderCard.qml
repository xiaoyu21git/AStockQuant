import QtQuick 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: card

    property bool hasSelectedStrategy: false
    property var strategySummary: null
    property var latestBacktestRecord: ({})
    property string selectedStrategyId: ""
    property string descriptionText: ""

    radius: 8
    color: "#111827"
    border.color: "#1F2937"
    border.width: 1
    implicitHeight: summaryColumn.implicitHeight + 24

    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"

    visible: hasSelectedStrategy

    ColumnLayout {
        id: summaryColumn
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: card.strategySummary
                    ? (card.strategySummary.strategyName || card.strategySummary.name || "未命名策略")
                    : "未选择策略"
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: textPrimary
            }

            Rectangle {
                radius: 999
                color: "#1E40AF"
                implicitWidth: strategyIdText.implicitWidth + 16
                implicitHeight: strategyIdText.implicitHeight + 8

                Text {
                    id: strategyIdText
                    anchors.centerIn: parent
                    text: "ID " + String(card.selectedStrategyId || "--")
                    font.pixelSize: 11
                    color: "white"
                }
            }

            Item { Layout.fillWidth: true }

            Text {
                text: card.latestBacktestRecord && Object.keys(card.latestBacktestRecord).length > 0
                    ? ("最近回测: " + String(card.latestBacktestRecord.recordedAt || "--"))
                    : "当前还没有回测记录"
                font.pixelSize: 12
                color: textSecondary
            }
        }

        Text {
            Layout.fillWidth: true
            text: card.descriptionText
            font.pixelSize: 12
            color: textSecondary
            wrapMode: Text.WordWrap
        }
    }
}
