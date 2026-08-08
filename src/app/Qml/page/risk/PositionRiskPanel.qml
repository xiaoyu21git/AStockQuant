import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: panelRoot

    // 数据流入
    required property var positionRisks
    required property var activeRiskStrategy
    required property real maxPositionPercent

    // 主题常量
    readonly property color pageText: "#E2E8F0"
    readonly property color secondaryText: "#94A3B8"
    readonly property color subtleText: "#64748B"
    readonly property color cardBg: "#1E293B"
    readonly property color cardBorder: "#334155"
    readonly property color cardBorderSoft: "#273449"
    readonly property color dangerRed: "#EF4444"
    readonly property color warningOrange: "#F97316"
    readonly property color primaryBlue: "#3B82F6"
    readonly property color progressBg: "#334155"
    readonly property int cardRadius: 20
    readonly property int cardInnerPadding: 24

    function badgeBackground(badgeType) {
        if (badgeType === "danger") return dangerRed
        if (badgeType === "warning") return warningOrange
        return primaryBlue
    }

    function badgeTextColor(badgeType) {
        if (badgeType === "danger") return "#FEE2E2"
        if (badgeType === "warning") return "#FED7AA"
        return "#BFDBFE"
    }

    function highestPositionRatio() {
        var maxVal = 0
        var risks = panelRoot.positionRisks || []
        for (var i = 0; i < risks.length; i++) {
            var r = parseFloat(risks[i].ratio) || 0
            if (r > maxVal) maxVal = r
        }
        return maxVal
    }

    function displayedWeightTotal() {
        var total = 0
        var risks = panelRoot.positionRisks || []
        for (var i = 0; i < risks.length; i++) {
            total += parseFloat(risks[i].ratio) || 0
        }
        return total
    }

    function aggregateDisplayedWeightRatio() {
        return displayedWeightTotal() / 100
    }

    function weightSummaryText() {
        var total = displayedWeightTotal().toFixed(1)
        var count = (panelRoot.positionRisks || []).length
        return "当前已展示 " + count + " 项权重指标，合计 " + total + "%"
    }

    Layout.fillWidth: true
    radius: cardRadius
    color: cardBg
    border.color: cardBorder
    border.width: 1
    implicitHeight: holdingsCardColumn.implicitHeight + 48

    ColumnLayout {
        id: holdingsCardColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: cardInnerPadding
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: "组合风险明细"
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: pageText
            }

            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            Repeater {
                model: ["对象", "权重占比", "风险状态", "操作建议"]
                delegate: Text {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: modelData
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: secondaryText
                }
            }
        }

        Repeater {
            model: panelRoot.positionRisks.length

            delegate: Rectangle {
                readonly property var positionRiskData: panelRoot.positionRisks[index] || ({})
                Layout.fillWidth: true
                Layout.preferredHeight: 42
                color: "transparent"
                border.color: cardBorderSoft
                border.width: index === panelRoot.positionRisks.length - 1 ? 0 : 1

                RowLayout {
                    anchors.fill: parent
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: positionRiskData.name || ""
                        font.pixelSize: 14
                        color: pageText
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: positionRiskData.ratio || ""
                        font.pixelSize: 14
                        color: pageText
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredHeight: 28
                        radius: 14
                        color: badgeBackground(positionRiskData.badgeType)

                        Text {
                            anchors.centerIn: parent
                            text: positionRiskData.badgeText || ""
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            color: badgeTextColor(positionRiskData.badgeType)
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: positionRiskData.badgeType === "danger"
                            ? "需要收缩配置"
                            : (positionRiskData.badgeType === "warning" ? "接近上限" : "继续观察")
                        font.pixelSize: 14
                        color: positionRiskData.badgeType === "danger"
                            ? dangerRed
                            : (positionRiskData.badgeType === "warning" ? warningOrange : secondaryText)
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Text {
            visible: panelRoot.positionRisks.length === 0
            text: activeRiskStrategy && Object.keys(activeRiskStrategy).length > 0
                ? "当前关联策略未提供可展示的组合权重明细"
                : "StrategyService 中暂无可用策略数据"
            font.pixelSize: 13
            color: subtleText
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 6
            radius: 3
            color: progressBg

            Rectangle {
                width: parent.width * highestPositionRatio() / 100
                height: parent.height
                radius: 3
                color: highestPositionRatio() >= maxPositionPercent
                    ? dangerRed
                    : warningOrange
            }
        }

        Text {
            text: "单项上限 " + maxPositionPercent.toFixed(0)
                + "% · 当前最高 " + highestPositionRatio().toFixed(1) + "%"
            font.pixelSize: 12
            color: secondaryText
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                text: "组合权重概览"
                font.pixelSize: 13
                font.weight: Font.Medium
                color: secondaryText
            }

            Text {
                text: weightSummaryText()
                font.pixelSize: 14
                color: pageText
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 6
                radius: 3
                color: progressBg

                Rectangle {
                    width: parent.width * aggregateDisplayedWeightRatio()
                    height: parent.height
                    radius: 3
                    color: primaryBlue
                }
            }

            Text {
                text: "已展示权重 " + displayedWeightTotal().toFixed(1) + "%"
                font.pixelSize: 12
                color: secondaryText
            }
        }
    }
}
