// FactorCard.qml
// 因子卡片组件 - 迁移到 BaseQuantCard 体系，降低首次进入因子库的首屏渲染开销
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import "./Base" as BaseComponents
import "../utils/FactorDataAdapter.js" as FactorAdapter

BaseQuantCard {
    id: factorCard

    BaseComponents.Constants {
        id: baseConstants
    }

    property string factorId: ""
    property string factorName: ""
    property string majorCategory: "动量类"

    property real icValue: 0.0
    property real irValue: 0.0
    property int validityDays: 20
    property real turnoverRate: 32

    property var groupReturns: []

    signal previewRequested()
    signal analyzeRequested()
    signal addToPortfolio()
    signal editRequested()
    signal deleteRequested()

    entityId: factorId
    entityType: "factor"
    displayName: factorName
    category: majorCategory
    categoryColor: FactorAdapter.getFactorCategoryColor(majorCategory)

    cardWidth: 190
    cardHeight: 260
    borderRadius: 10
    enableRealTimeFeedback: true
    enableCardClick: true
    showMiniChart: true
    showGroupReturns: false

    performanceMetrics: [
        {
            label: "IC",
            value: icValue,
            format: "%.3f",
            color: categoryColor,
            tooltip: "信息系数"
        },
        {
            label: "IR",
            value: irValue,
            format: "%.2f",
            color: categoryColor,
            tooltip: "信息比率"
        }
    ]

    additionalMetrics: [
        {
            label: "换手率",
            value: turnoverRate,
            format: "%.0f",
            unit: "%/年",
            color: categoryColor,
            tooltip: "年化换手率"
        },
        {
            label: "有效期",
            value: validityDays,
            format: "%d",
            unit: "天",
            color: categoryColor,
            tooltip: "信号有效期"
        }
    ]

    chartData: calculateChartData()

    Item {
        id: factorActions
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: spacingXLarge
        anchors.bottomMargin: spacingMedium
        height: factorCard.showActions ? 32 : 0
        visible: factorCard.showActions
        z: 2

        Row {
            anchors.centerIn: parent
            spacing: 4

            ActionButton {
                icon: "👁️"
                buttonColor: factorCard.categoryColor
                onClicked: factorCard.previewRequested()
            }

            ActionButton {
                icon: "📊"
                buttonColor: factorCard.categoryColor
                onClicked: factorCard.analyzeRequested()
            }

            ActionButton {
                icon: "➕"
                buttonColor: baseConstants.profitGreen
                onClicked: factorCard.addToPortfolio()
            }

            ActionButton {
                icon: "✏️"
                buttonColor: baseConstants.warningAmber
                onClicked: factorCard.editRequested()
            }

            ActionButton {
                icon: "🗑️"
                buttonColor: baseConstants.lossRed
                onClicked: factorCard.deleteRequested()
            }
        }
    }

    component ActionButton: Rectangle {
        property string icon: ""
        property color buttonColor: baseConstants.accentBlue
        signal clicked()

        width: 28
        height: 28
        radius: 6
        color: Qt.rgba(buttonColor.r, buttonColor.g, buttonColor.b, 0.2)

        Text {
            anchors.centerIn: parent
            text: icon
            font.pixelSize: 12
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.clicked()
        }
    }

    function calculateChartData() {
        if (groupReturns && groupReturns.length > 0) {
            return groupReturns
        }

        var fallbackData = []
        if (!isNaN(icValue)) {
            fallbackData.push(icValue)
        }
        if (!isNaN(irValue)) {
            fallbackData.push(irValue)
        }
        if (!isNaN(turnoverRate)) {
            fallbackData.push(Math.max(-1, Math.min(1, turnoverRate / 100.0)))
        }
        if (!isNaN(validityDays)) {
            fallbackData.push(Math.max(-1, Math.min(1, validityDays / 30.0)))
        }
        return fallbackData
    }

    Component.onCompleted: {
        if (typeof groupReturns === "undefined" || groupReturns === null) {
            groupReturns = []
        }
    }
}
