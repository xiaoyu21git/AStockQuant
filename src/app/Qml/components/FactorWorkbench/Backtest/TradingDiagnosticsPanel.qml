import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

RowLayout {
    id: panelRoot
    // -- Interface: data in --
    required property QtObject page

    // -- Interface: events out --
    signal toggleFormalTradingDetails()

                    spacing: 16

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: panelRoot.page.hasTradingPreview()
                        radius: 12
                        color: "#111827"
                        border.width: 1
                        border.color: panelRoot.page.tradingPreviewAccentColor(panelRoot.page.tradingPreviewStatus())

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        text: "统一交易预执行"
                                        font.pixelSize: 15
                                        font.weight: Font.DemiBold
                                        color: "#F8FAFC"
                                    }

                                    Text {
                                        text: "执行诊断，不参与研究指标评分"
                                        font.pixelSize: 11
                                        color: "#94A3B8"
                                    }
                                }

                                Rectangle {
                                    radius: 10
                                    color: Qt.rgba(Qt.color(panelRoot.page.tradingPreviewAccentColor(panelRoot.page.tradingPreviewStatus())).r,
                                                   Qt.color(panelRoot.page.tradingPreviewAccentColor(panelRoot.page.tradingPreviewStatus())).g,
                                                   Qt.color(panelRoot.page.tradingPreviewAccentColor(panelRoot.page.tradingPreviewStatus())).b,
                                                   0.16)
                                    border.width: 1
                                    border.color: panelRoot.page.tradingPreviewAccentColor(panelRoot.page.tradingPreviewStatus())
                                    implicitWidth: tradingPreviewStatusText.implicitWidth + 16
                                    implicitHeight: tradingPreviewStatusText.implicitHeight + 8

                                    Text {
                                        id: tradingPreviewStatusText
                                        anchors.centerIn: parent
                                        text: panelRoot.page.tradingPreviewStatusLabel(panelRoot.page.tradingPreviewStatus())
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                        color: panelRoot.page.tradingPreviewAccentColor(panelRoot.page.tradingPreviewStatus())
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: panelRoot.page.tradingPreviewSecondaryMessage()
                                font.pixelSize: 12
                                color: "#CBD5E1"
                                wrapMode: Text.WordWrap
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                visible: panelRoot.page.hasTradingPreview()
                                spacing: 10

                                Repeater {
                                    model: [
                                        { label: "目标持仓", value: panelRoot.page.tradingPreviewCountText(panelRoot.page.currentTradingPreview().targetPositionCount) },
                                        { label: "委托计划", value: panelRoot.page.tradingPreviewCountText(panelRoot.page.currentTradingPreview().orderPlanCount) },
                                        { label: "已接受", value: panelRoot.page.tradingPreviewCountText(panelRoot.page.currentTradingPreview().acceptedOrderCount) },
                                        { label: "成交回报", value: panelRoot.page.tradingPreviewCountText(panelRoot.page.currentTradingPreview().fillCount) }
                                    ]

                                    delegate: Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 48
                                        radius: 10
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#243041"

                                        Column {
                                            anchors.centerIn: parent
                                            spacing: 2

                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: modelData.value
                                                font.pixelSize: 15
                                                font.weight: Font.Bold
                                                color: "#F8FAFC"
                                            }

                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: modelData.label
                                                font.pixelSize: 10
                                                color: "#94A3B8"
                                            }
                                        }
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: String(panelRoot.page.currentTradingPreview().riskReason || "").trim().length > 0
                                text: "风险原因: " + String(panelRoot.page.currentTradingPreview().riskReason || "").trim()
                                font.pixelSize: 11
                                color: panelRoot.page.tradingPreviewAccentColor(panelRoot.page.tradingPreviewStatus())
                                wrapMode: Text.WordWrap
                            }
                        }
                    

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: panelRoot.page.hasFormalTradingExecution()
                        radius: 12
                        color: "#111827"
                        border.width: 1
                        border.color: panelRoot.page.formalTradingAccentColor(panelRoot.page.formalTradingStatus())

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        text: "正式统一交易回放"
                                        font.pixelSize: 15
                                        font.weight: Font.DemiBold
                                        color: "#F8FAFC"
                                    }

                                    Text {
                                        text: "账户轨迹与成交结果，不参与研究指标评分"
                                        font.pixelSize: 11
                                        color: "#94A3B8"
                                    }
                                }

                                Rectangle {
                                    radius: 10
                                    color: Qt.rgba(Qt.color(panelRoot.page.formalTradingAccentColor(panelRoot.page.formalTradingStatus())).r,
                                                   Qt.color(panelRoot.page.formalTradingAccentColor(panelRoot.page.formalTradingStatus())).g,
                                                   Qt.color(panelRoot.page.formalTradingAccentColor(panelRoot.page.formalTradingStatus())).b,
                                                   0.16)
                                    border.width: 1
                                    border.color: panelRoot.page.formalTradingAccentColor(panelRoot.page.formalTradingStatus())
                                    implicitWidth: formalTradingStatusText.implicitWidth + 16
                                    implicitHeight: formalTradingStatusText.implicitHeight + 8

                                    Text {
                                        id: formalTradingStatusText
                                        anchors.centerIn: parent
                                        text: panelRoot.page.formalTradingStatusLabel(panelRoot.page.formalTradingStatus())
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                        color: panelRoot.page.formalTradingAccentColor(panelRoot.page.formalTradingStatus())
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: panelRoot.page.formalTradingSecondaryMessage()
                                font.pixelSize: 12
                                color: "#CBD5E1"
                                wrapMode: Text.WordWrap
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Repeater {
                                    model: [
                                        { label: "计划调仓", value: panelRoot.page.tradingPreviewCountText(panelRoot.page.currentFormalTradingExecution().scheduledRebalanceCount) },
                                        { label: "实际调仓", value: panelRoot.page.tradingPreviewCountText(panelRoot.page.currentFormalTradingExecution().executedRebalanceCount) },
                                        { label: "风险阻断", value: panelRoot.page.tradingPreviewCountText(panelRoot.page.currentFormalTradingExecution().blockedRebalanceCount) },
                                        { label: "成交回报", value: panelRoot.page.tradingPreviewCountText(panelRoot.page.currentFormalTradingExecution().fillCount) }
                                    ]

                                    delegate: Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 48
                                        radius: 10
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#243041"

                                        Column {
                                            anchors.centerIn: parent
                                            spacing: 2

                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: modelData.value
                                                font.pixelSize: 15
                                                font.weight: Font.Bold
                                                color: "#F8FAFC"
                                            }

                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: modelData.label
                                                font.pixelSize: 10
                                                color: "#94A3B8"
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                id: formalTradingCurvePanel
                                Layout.fillWidth: true
                                Layout.preferredHeight: 78
                                radius: 10
                                color: "#0F172A"
                                border.width: 1
                                border.color: "#243041"
                                property int hoveredCurveIndex: -1
                                property bool curveTooltipVisible: false
                                property string curveTooltipText: ""
                                property real curveTooltipX: 0
                                property real curveTooltipY: 0

                                Canvas {
                                    id: formalTradingCurveCanvas
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    antialiasing: true

                                    onPaint: {
                                        var context = getContext("2d")
                                        context.clearRect(0, 0, width, height)

                                        var series = panelRoot.page.formalTradingCurveSeries()
                                        if (series.length === 0) {
                                            context.fillStyle = "#94A3B8"
                                            context.font = "11px sans-serif"
                                            context.fillText("暂无总资产曲线", 8, 18)
                                            return
                                        }

                                        var minValue = panelRoot.page.formalTradingCurveMinValue()
                                        var maxValue = panelRoot.page.formalTradingCurveMaxValue()
                                        var leftPadding = 4
                                        var rightPadding = 4
                                        var topPadding = 16
                                        var bottomPadding = 6
                                        var startValue = series[0]
                                        var baselineY = panelRoot.page.formalTradingCurvePointY(startValue,
                                                                                       minValue,
                                                                                       maxValue,
                                                                                       height,
                                                                                       topPadding,
                                                                                       bottomPadding)

                                        context.strokeStyle = "#334155"
                                        context.lineWidth = 1
                                        context.beginPath()
                                        context.moveTo(leftPadding, baselineY)
                                        context.lineTo(width - rightPadding, baselineY)
                                        context.stroke()

                                        context.strokeStyle = panelRoot.page.formalTradingAccentColor(panelRoot.page.formalTradingStatus())
                                        context.lineWidth = 2
                                        context.beginPath()
                                        for (var pointIndex = 0; pointIndex < series.length; pointIndex++) {
                                            var x = panelRoot.page.formalTradingCurvePointX(pointIndex,
                                                                                  series.length,
                                                                                  width,
                                                                                  leftPadding,
                                                                                  rightPadding)
                                            var y = panelRoot.page.formalTradingCurvePointY(series[pointIndex],
                                                                                  minValue,
                                                                                  maxValue,
                                                                                  height,
                                                                                  topPadding,
                                                                                  bottomPadding)
                                            if (pointIndex === 0) {
                                                context.moveTo(x, y)
                                            } else {
                                                context.lineTo(x, y)
                                            }
                                        }
                                        context.stroke()

                                        if (formalTradingCurvePanel.hoveredCurveIndex >= 0
                                                && formalTradingCurvePanel.hoveredCurveIndex < series.length) {
                                            var highlightIndex = formalTradingCurvePanel.hoveredCurveIndex
                                            var highlightX = panelRoot.page.formalTradingCurvePointX(highlightIndex,
                                                                                           series.length,
                                                                                           width,
                                                                                           leftPadding,
                                                                                           rightPadding)
                                            var highlightY = panelRoot.page.formalTradingCurvePointY(series[highlightIndex],
                                                                                           minValue,
                                                                                           maxValue,
                                                                                           height,
                                                                                           topPadding,
                                                                                           bottomPadding)

                                            context.strokeStyle = Qt.rgba(Qt.color(panelRoot.page.formalTradingAccentColor(panelRoot.page.formalTradingStatus())).r,
                                                                          Qt.color(panelRoot.page.formalTradingAccentColor(panelRoot.page.formalTradingStatus())).g,
                                                                          Qt.color(panelRoot.page.formalTradingAccentColor(panelRoot.page.formalTradingStatus())).b,
                                                                          0.32)
                                            context.lineWidth = 1
                                            context.beginPath()
                                            context.moveTo(highlightX, topPadding)
                                            context.lineTo(highlightX, height - bottomPadding)
                                            context.stroke()

                                            context.fillStyle = "#F8FAFC"
                                            context.beginPath()
                                            context.arc(highlightX, highlightY, 3.5, 0, Math.PI * 2)
                                            context.fill()

                                            context.strokeStyle = panelRoot.page.formalTradingAccentColor(panelRoot.page.formalTradingStatus())
                                            context.lineWidth = 1.5
                                            context.beginPath()
                                            context.arc(highlightX, highlightY, 5.5, 0, Math.PI * 2)
                                            context.stroke()
                                        }
                                    }

                                    onWidthChanged: requestPaint()
                                    onHeightChanged: requestPaint()
                                    Connections {
                                        target: panelRoot.page
                                        function onDisplayedBacktestResultChanged() { formalTradingCurveCanvas.requestPaint() }
                                    }
                                    Component.onCompleted: requestPaint()
                                }

                                MouseArea {
                                    anchors.fill: formalTradingCurveCanvas
                                    hoverEnabled: true
                                    onPositionChanged: function(mouse) {
                                        var nearestIndex = panelRoot.page.formalTradingCurveNearestIndex(mouse.x, formalTradingCurveCanvas.width)
                                        if (nearestIndex < 0) {
                                            formalTradingCurvePanel.hoveredCurveIndex = -1
                                            formalTradingCurvePanel.curveTooltipVisible = false
                                            formalTradingCurveCanvas.requestPaint()
                                            return
                                        }

                                        formalTradingCurvePanel.hoveredCurveIndex = nearestIndex
                                        formalTradingCurvePanel.curveTooltipText = panelRoot.page.formalTradingCurveTooltipText(nearestIndex)
                                        formalTradingCurvePanel.curveTooltipVisible = formalTradingCurvePanel.curveTooltipText.length > 0
                                        formalTradingCurvePanel.curveTooltipX = mouse.x + formalTradingCurveCanvas.anchors.leftMargin + 12
                                        formalTradingCurvePanel.curveTooltipY = mouse.y + formalTradingCurveCanvas.anchors.topMargin - 46
                                        formalTradingCurveCanvas.requestPaint()
                                    }
                                    onExited: {
                                        formalTradingCurvePanel.hoveredCurveIndex = -1
                                        formalTradingCurvePanel.curveTooltipVisible = false
                                        formalTradingCurveCanvas.requestPaint()
                                    }
                                }

                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 10
                                    anchors.top: parent.top
                                    anchors.topMargin: 8
                                    text: "总资产曲线"
                                    font.pixelSize: 10
                                    color: "#94A3B8"
                                }

                                Text {
                                    anchors.right: parent.right
                                    anchors.rightMargin: 10
                                    anchors.top: parent.top
                                    anchors.topMargin: 8
                                    text: panelRoot.page.formatAssetMetric(panelRoot.page.currentFormalTradingExecution().endingTotalAsset)
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                    color: panelRoot.page.formalTradingAccentColor(panelRoot.page.formalTradingStatus())
                                }

                                Rectangle {
                                    visible: formalTradingCurvePanel.curveTooltipVisible
                                    radius: 8
                                    color: "#111827"
                                    border.width: 1
                                    border.color: panelRoot.page.formalTradingAccentColor(panelRoot.page.formalTradingStatus())
                                    z: 2
                                    x: Math.min(Math.max(8, formalTradingCurvePanel.curveTooltipX), formalTradingCurvePanel.width - width - 8)
                                    y: Math.min(Math.max(24, formalTradingCurvePanel.curveTooltipY), formalTradingCurvePanel.height - height - 8)
                                    width: formalTradingCurveTooltipText.implicitWidth + 16
                                    height: formalTradingCurveTooltipText.implicitHeight + 12

                                    Text {
                                        id: formalTradingCurveTooltipText
                                        anchors.centerIn: parent
                                        text: formalTradingCurvePanel.curveTooltipText
                                        font.pixelSize: 10
                                        color: "#E2E8F0"
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Text {
                                    Layout.fillWidth: true
                                    text: "执行日 "
                                          + panelRoot.page.tradingPreviewCountText(panelRoot.page.normalizedListValue(panelRoot.page.currentFormalTradingExecution().executionDates).length)
                                          + " 个 · 已接受 "
                                          + panelRoot.page.tradingPreviewCountText(panelRoot.page.currentFormalTradingExecution().acceptedOrderCount)
                                          + " 笔 · 期末总资产 "
                                          + panelRoot.page.formatAssetMetric(panelRoot.page.currentFormalTradingExecution().endingTotalAsset)
                                          + " · 现金 "
                                          + panelRoot.page.formatAssetMetric(panelRoot.page.currentFormalTradingExecution().endingCash)
                                          + " · 持仓市值 "
                                          + panelRoot.page.formatAssetMetric(panelRoot.page.currentFormalTradingExecution().endingMarketValue)
                                    font.pixelSize: 11
                                    color: "#94A3B8"
                                    wrapMode: Text.WordWrap
                                }

                                Rectangle {
                                    visible: panelRoot.page.formalTradingDetailRows().length > 0
                                    radius: 10
                                    color: panelRoot.page.formalTradingDetailsExpanded ? "#1D4ED8" : "#0F172A"
                                    border.width: 1
                                    border.color: panelRoot.page.formalTradingDetailsExpanded ? "#60A5FA" : "#334155"
                                    implicitWidth: formalTradingDetailsButtonText.implicitWidth + 18
                                    implicitHeight: formalTradingDetailsButtonText.implicitHeight + 10

                                    Text {
                                        id: formalTradingDetailsButtonText
                                        anchors.centerIn: parent
                                        text: panelRoot.page.formalTradingDetailsExpanded ? "收起明细" : "查看明细"
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                        color: "#E2E8F0"
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: panelRoot.toggleFormalTradingDetails()
                                    }
                                }
                            }
                        }
                    }
                }
}
