import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../../utils/TradingConstants.js" as Const
import "../../../utils/TradingWidgetBase.js" as Base

// ============================================================================
// ExecutionLogWidget — 执行日志 (动态布局适配)
//
// optimalHeight = 标题(28) + 7行*行高(28*7=196) + 边距(16) = 240 ≈ 280
//
// 密度模式:
//   Compact  (< 155px):  单行文本, 仅 kind+title
//   Normal   (155-320px): time+badge+title+detail
//   Expanded (> 320px):  全部字段 + 策略ID
//
// 日志顺序: 最新在底部 (正序), 新条目自动滚底
// 自动滚动: 用户上滚暂停, 滚回底部恢复
//
// Bridge: TradeExecutionBridge (信号监听)
// ============================================================================

Rectangle {
    id: root
    property var widgetConfig: ({})
    color: Const.tradingPanelBg
    radius: 10
    border.color: Const.tradingPanelBorder
    border.width: 1
    clip: true

    // ============ 动态适配 ============
    readonly property real optimalHeight: 280
    property real scaleFactor: Base.computeScaleFactor(height, optimalHeight)
    property string densityMode: "normal"

    onHeightChanged: {
        var newMode = Base.computeDensityMode(height, optimalHeight, densityMode)
        if (newMode !== densityMode) densityMode = newMode
    }

    function s(v) { return Base.scaleValue(v, scaleFactor) }

    // ============ 日志状态 ============
    readonly property int maxEntries: 100

    // 日志数组: [{ time, kind, badge, title, detail, severity, strategyId }]
    property var logEntries: []

    function currentTime() {
        return Qt.formatDateTime(new Date(), "hh:mm:ss")
    }

    function appendLog(kind, title, detail, severity, strategyId) {
        var entry = {
            time: currentTime(),
            kind: kind || "system",
            badge: Base.logBadgeText(kind),
            title: title || "",
            detail: detail || "",
            severity: severity || "info",
            strategyId: strategyId || ""
        }
        var next = logEntries.slice()
        next.push(entry)  // 追加到末尾 (正序)
        if (next.length > maxEntries) {
            next = next.slice(next.length - maxEntries)
        }
        logEntries = next
    }

    function clearLogs() {
        logEntries = []
    }

    // ============ Bridge 信号监听 ============

    Connections {
        target: Bridge.TradeExecutionBridge
        enabled: !!Bridge.TradeExecutionBridge

        // 委托提交
        function onOrderRequestPublished(orderRequest) {
            if (!orderRequest) return
            var sym = String(orderRequest.symbol || "")
            var side = Base.sideLabel(orderRequest.side)
            var qty = Number(orderRequest.quantity || 0)
            var price = Number(orderRequest.price || 0)
            var detail = sym + " " + side + " " + qty + "股"
            if (price > 0) detail += " @ " + price.toFixed(2)
            root.appendLog("request", "委托提交", detail, "info",
                String(orderRequest.strategyId || ""))
        }

        // 状态变更
        function onOrderStatusPublished(orderStatus) {
            if (!orderStatus) return
            var badge = Base.orderStatusBadge(orderStatus.rawStatus || orderStatus.status)
            var sym = String(orderStatus.symbol || "")
            var filledInfo = ""
            if (Number(orderStatus.filledQty || 0) > 0) {
                filledInfo = " 已成交" + Number(orderStatus.filledQty)
            }
            var detail = sym + " " + badge + filledInfo
            if (orderStatus.message) detail += " · " + String(orderStatus.message)

            var sev = "info"
            var st = String(orderStatus.rawStatus || orderStatus.status || "")
            if (st === "REJECTED") sev = "error"
            else if (st === "FILLED") sev = "success"
            else if (st === "PARTIAL_FILLED") sev = "warning"

            root.appendLog("status", "状态更新", detail, sev,
                String(orderStatus.strategyId || ""))
        }

        // 成交回报
        function onTradeFillPublished(tradeFill) {
            if (!tradeFill) return
            var sym = String(tradeFill.symbol || "")
            var price = Number(tradeFill.price || 0)
            var qty = Number(tradeFill.quantity || 0)
            var detail = sym + " " + price.toFixed(2) + " × " + qty
            root.appendLog("fill", "成交回报", detail, "success",
                String(tradeFill.strategyId || ""))
        }

        // 提交结果 (仅记录拒绝)
        function onOrderSubmitResult(result) {
            if (!result || result.accepted) return
            var sym = String(result.symbol || "")
            var reason = String(result.reason || "未知原因")
            root.appendLog("status", "提交拒绝", sym + " · " + reason, "error",
                String(result.strategyId || ""))
        }
    }

    // ============ 布局 ============
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: s(6)
        spacing: 0

        // --- 标题行 ---
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: s(28)
            spacing: s(6)

            Text {
                text: "执行日志"
                color: Const.tradingTitleText
                font.pixelSize: s(12)
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Text {
                text: String(logEntries.length) + " 条"
                color: Const.tradingEmptyText
                font.pixelSize: s(9)
            }
            // 清空按钮
            Rectangle {
                width: s(40); height: s(20); radius: 4
                color: clearMa.containsMouse ? "rgba(239,68,68,0.15)" : "transparent"
                border.color: clearMa.containsMouse ? "#EF4444" : "#334155"
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "清空"; color: clearMa.containsMouse ? "#EF4444" : "#64748B"
                    font.pixelSize: s(9)
                }

                MouseArea {
                    id: clearMa
                    anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.clearLogs()
                }
            }
        }

        Item { Layout.fillHeight: true; Layout.preferredHeight: s(2) }

        // --- 日志列表 ---
        ListView {
            id: logList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: logEntries
            clip: true
            spacing: s(1)

            property bool autoScroll: true

            onCountChanged: {
                if (autoScroll) {
                    Qt.callLater(function() { logList.positionViewAtEnd() })
                }
            }

            onContentYChanged: {
                if (atYEnd) {
                    autoScroll = true
                }
            }
            onMovementStarted: {
                autoScroll = false
            }

            delegate: Rectangle {
                width: logList.width
                height: root.densityMode === "compact" ? root.s(18)
                      : root.densityMode === "expanded" ? root.s(28)
                      : root.s(24)
                color: "#0b1625"
                radius: 4

                // Compact: 单行 kind+title
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: root.s(4)
                    spacing: root.s(4)
                    visible: root.densityMode === "compact"

                    LogBadge { bw: root.s(30); bh: root.s(14); br: 3
                               bt: modelData.badge; btc: Base.severityColor(modelData.severity)
                               bfs: root.s(8) }
                    Text {
                        text: modelData.title
                        color: "#F1F5F9"
                        font.pixelSize: root.s(9)
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                // Normal: time+badge+title+detail
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: root.s(4)
                    spacing: root.s(4)
                    visible: root.densityMode === "normal"

                    LogTime { t: modelData.time }
                    LogBadge { bw: root.s(32); bh: root.s(16); br: 4
                               bt: modelData.badge; btc: Base.severityColor(modelData.severity)
                               bfs: root.s(8) }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: modelData.title
                            color: "#F1F5F9"
                            font.pixelSize: root.s(10)
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: modelData.detail
                            color: "#94A3B8"
                            font.pixelSize: root.s(9)
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            visible: modelData.detail.length > 0
                        }
                    }
                }

                // Expanded: time+badge+title+detail+strategyId
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: root.s(5)
                    spacing: root.s(6)
                    visible: root.densityMode === "expanded"

                    LogTime { t: modelData.time }
                    LogBadge { bw: root.s(40); bh: root.s(20); br: 6
                               bt: modelData.badge; btc: Base.severityColor(modelData.severity)
                               bfs: root.s(9) }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: root.s(1)
                        Text {
                            text: modelData.title
                            color: "#F1F5F9"
                            font.pixelSize: root.s(11)
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: modelData.detail
                            color: "#94A3B8"
                            font.pixelSize: root.s(10)
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            visible: modelData.detail.length > 0
                        }
                    }
                    Text {
                        text: modelData.strategyId ? String(modelData.strategyId).substring(0, 8) : ""
                        color: "#64748B"
                        font.pixelSize: root.s(8)
                        visible: modelData.strategyId && modelData.strategyId.length > 0
                    }
                }
            }
        }

    }

    // 空状态 (在 ColumnLayout 外部, 直接居中)
    Text {
        anchors.centerIn: parent
        visible: logEntries.length === 0
        text: "策略发起委托后，这里会显示实时执行日志"
        color: Const.tradingEmptyText
        font.pixelSize: s(10)
    }

    // ============ 内部组件 ============
    component LogTime: Text {
        property string t: ""
        text: t; color: "#64748B"; font.pixelSize: root.s(9)
    }

    component LogBadge: Rectangle {
        property real bw: 30; property real bh: 14; property real br: 3
        property string bt: ""; property color btc: "#3B82F6"; property int bfs: 8
        width: bw; height: bh; radius: br
        color: "transparent"; border.color: btc; border.width: 1
        Text {
            anchors.centerIn: parent
            text: bt; color: btc; font.pixelSize: bfs; font.weight: Font.Medium
        }
    }
}
