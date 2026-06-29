import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../../utils/TradingConstants.js" as Const
import "../../../utils/TradingWidgetBase.js" as Base

// ============================================================================
// OrderListWidget — 委托成交列表 (动态布局适配)
//
// optimalHeight = 标题(28) + 标签栏(24) + 表头(22) + 7行*行高(28*7=196) + 统计条(20) + 边距(16) ≈ 306
//                取整 = 300
//
// 密度模式:
//   Compact  (< 165px):  仅委托列表, 3列 (symbol+side+status)
//   Normal   (165-345px): 标签切换, 委托+成交双视图, 5-6列
//   Expanded (> 345px):  双标签+底部统计, 全部列
//
// 列优先级 (applyColumnPriority):
//   1=必须, 2=高, 3=中, 4=低, 5=仅Expanded
//
// Bridge: TradeExecutionBridge
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
    readonly property real optimalHeight: 300
    property real scaleFactor: Base.computeScaleFactor(height, optimalHeight)
    property string densityMode: "normal"

    onHeightChanged: {
        var newMode = Base.computeDensityMode(height, optimalHeight, densityMode)
        if (newMode !== densityMode) {
            densityMode = newMode
            Qt.callLater(applyColumns)
        }
    }

    onWidthChanged: Qt.callLater(applyColumns)

    function s(v) { return Base.scaleValue(v, scaleFactor) }

    // ============ Bridge 数据 ============
    property var allOrders: Bridge.TradeExecutionBridge
        && Bridge.TradeExecutionBridge.recentOrders
        ? Bridge.TradeExecutionBridge.recentOrders : []

    Connections {
        target: Bridge.TradeExecutionBridge
        function onRecentOrdersChanged() {
            allOrders = Bridge.TradeExecutionBridge.recentOrders || []
        }
    }

    // 委托列表: 全部订单
    readonly property var orderList: allOrders

    // 成交历史: 筛选 FILLED
    readonly property var fillList: {
        var result = []
        for (var i = 0; i < allOrders.length; i++) {
            var st = String(allOrders[i].rawStatus || allOrders[i].status || "")
            if (st === "FILLED") result.push(allOrders[i])
        }
        return result
    }

    // 统计数据
    readonly property int totalCount: allOrders.length
    readonly property int filledCount: fillList.length
    readonly property int rejectedCount: {
        var c = 0
        for (var j = 0; j < allOrders.length; j++) {
            var s = String(allOrders[j].rawStatus || allOrders[j].status || "")
            if (s === "REJECTED") c++
        }
        return c
    }

    // ============ 列定义 ============
    property var columns: [
        { key: "symbol",   label: "代码", baseWidth: 52, priority: 1, visible: true },
        { key: "side",     label: "方向", baseWidth: 24, priority: 2, visible: true },
        { key: "status",   label: "状态", baseWidth: 36, priority: 2, visible: true },
        { key: "time",     label: "时间", baseWidth: 50, priority: 3, visible: true },
        { key: "price",    label: "价格", baseWidth: 40, priority: 3, visible: true },
        { key: "quantity", label: "数量", baseWidth: 36, priority: 4, visible: true },
        { key: "filledQty",label: "已成交",baseWidth: 36, priority: 5, visible: false },
        { key: "orderId",  label: "委托编号",baseWidth: 80,priority: 5, visible: false },
        { key: "strategyId",label:"策略",   baseWidth: 60, priority: 5, visible: false }
    ]

    function visibleColumn(key) {
        for (var i = 0; i < columns.length; i++) {
            if (columns[i].key === key) return columns[i].visible
        }
        return false
    }

    function applyColumns() {
        Base.applyColumnPriority(columns, width, scaleFactor)
    }

    Component.onCompleted: applyColumns()

    // ============ 当前视图 ============
    property int currentTab: 0  // 0 = 委托, 1 = 成交
    readonly property var currentModel: currentTab === 0 ? orderList : fillList

    // ============ 布局 ============
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: s(6)
        spacing: 0

        // --- 标题行 + 标签切换 ---
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: s(28)
            spacing: s(6)

            Text {
                text: "委托成交"
                color: Const.tradingTitleText
                font.pixelSize: s(12)
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }

            // 标签切换 (Normal+)
            TabChip {
                label: "委托"
                active: currentTab === 0
                onClicked: { currentTab = 0 }
                visible: densityMode !== "compact"
            }
            TabChip {
                label: "成交"
                active: currentTab === 1
                onClicked: { currentTab = 1 }
                visible: densityMode !== "compact"
            }
        }

        Item { Layout.fillHeight: true; Layout.preferredHeight: s(2) }

        // --- 表头 ---
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: s(16)
            spacing: s(2)

            ColHeader { l: "代码";     pw: s(52); visible: visibleColumn("symbol") }
            ColHeader { l: "方向";     pw: s(24); visible: visibleColumn("side") }
            ColHeader { l: "状态";     pw: s(36); visible: visibleColumn("status") }
            ColHeader { l: "时间";     pw: s(50); visible: visibleColumn("time") }
            ColHeader { l: "价格";     pw: s(40); visible: visibleColumn("price") }
            ColHeader { l: "数量";     pw: s(36); visible: visibleColumn("quantity") }
            ColHeader { l: "已成交";   pw: s(36); visible: visibleColumn("filledQty") }
            ColHeader { l: "委托编号"; pw: s(80); visible: visibleColumn("orderId") }
            ColHeader { l: "策略";     pw: s(60); visible: visibleColumn("strategyId") }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: "#334155"
        }

        // --- 订单列表 ---
        ListView {
            id: orderListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: currentModel
            clip: true
            spacing: s(1)

            delegate: RowLayout {
                width: orderListView.width
                spacing: root.s(2)
                Layout.preferredHeight: root.s(20)

                OrderCell { cv: String(modelData.symbol || "").replace(/(\d{6}).*/, "$1") || "--"
                            cc: "#F1F5F9"; cpw: root.s(52); visible: visibleColumn("symbol") }
                OrderCell { cv: Base.sideLabel(modelData.side)
                            cc: Base.sideColor(modelData.side); cpw: root.s(24); visible: visibleColumn("side") }
                OrderCell { cv: Base.orderStatusBadge(modelData.rawStatus || modelData.status)
                            cc: Base.orderStatusColor(modelData.rawStatus || modelData.status)
                            cpw: root.s(36); visible: visibleColumn("status") }
                OrderCell { cv: formatTime(modelData.submittedAt)
                            cc: "#94A3B8"; cpw: root.s(50); visible: visibleColumn("time") }
                OrderCell { cv: Number(modelData.price || 0).toFixed(2)
                            cc: "#F1F5F9"; cpw: root.s(40); visible: visibleColumn("price") }
                OrderCell { cv: String(Number(modelData.quantity || 0))
                            cc: "#94A3B8"; cpw: root.s(36); visible: visibleColumn("quantity") }
                OrderCell { cv: String(Number(modelData.filledQty || 0))
                            cc: "#94A3B8"; cpw: root.s(36); visible: visibleColumn("filledQty") }
                OrderCell { cv: String(modelData.brokerOrderId || modelData.clientOrderId || "--")
                            cc: "#64748B"; cpw: root.s(80); visible: visibleColumn("orderId")
                            cElide: true }
                OrderCell { cv: String(modelData.strategyId || "").substring(0, 8) || "--"
                            cc: "#64748B"; cpw: root.s(60); visible: visibleColumn("strategyId")
                            cElide: true }
            }
        }

        // --- 底部统计条 (Expanded) ---
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: s(18)
            spacing: s(10)
            visible: densityMode === "expanded"

            Text { text: "共 " + totalCount + " 条委托"; color: Const.tradingEmptyText; font.pixelSize: s(9) }
            Text { text: "│"; color: "#334155"; font.pixelSize: s(9) }
            Text { text: "已成 " + filledCount; color: "#10B981"; font.pixelSize: s(9) }
            Text { text: "拒单 " + rejectedCount; color: "#EF4444"; font.pixelSize: s(9) }
            Item { Layout.fillWidth: true }
        }
    }

    // ============ 内部组件 ============

    component TabChip: Rectangle {
        property string label: ""
        property bool active: false
        signal clicked()

        width: root.s(36)
        height: root.s(20)
        radius: 4
        color: active ? Const.tradingButtonBg : "transparent"
        border.color: active ? Const.tradingInputActiveBorder : "#334155"
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: label
            color: active ? Const.tradingLightBlue : "#64748B"
            font.pixelSize: root.s(9)
            font.weight: active ? Font.DemiBold : Font.Normal
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.clicked()
        }
    }

    component ColHeader: Text {
        property string l: ""
        property real pw: 40
        text: l
        color: "#64748B"
        font.pixelSize: root.s(8)
        Layout.preferredWidth: pw
    }

    component OrderCell: Text {
        property string cv: ""
        property color cc: "#F1F5F9"
        property real cpw: 40
        property bool cElide: true
        text: cv
        color: cc
        font.pixelSize: root.s(9)
        Layout.preferredWidth: cpw
        elide: cElide ? Text.ElideRight : Text.ElideNone
    }

    // ============ 辅助函数 ============
    function formatTime(isoStr) {
        if (!isoStr) return "--"
        var s = String(isoStr)
        // 提取 HH:mm:ss 部分
        var m = s.match(/(\d{2}:\d{2}:\d{2})/)
        return m ? m[1] : s.substring(0, 8)
    }
}
