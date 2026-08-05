import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../../utils/TradingConstants.js" as Const
import "../../../utils/TradingWidgetBase.js" as Base

// ============================================================================
// PositionListWidget — 持仓列表 (动态布局适配)
//
// optimalHeight = 标题(28) + 分组头*5(22*5=110) + 7行*行高(28*7=196) + 汇总(20) + 边距(16) ≈ 370
//                但分组默认折叠, 实际可见内容较少. 取 optimalHeight = 320.
//
// 密度模式:
//   Compact  (< 175px):  不分组, 单列表, 3列 (symbol+quantity+pnl)
//   Normal   (175-370px): 分组折叠, 5-6列
//   Expanded (> 370px):  全部分组展开, 底部汇总条, 全部列
//
// 分组: stock → margin_buy → margin_sell → futures → options (空组不显示)
//
// Bridge: PositionAccountBridge
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
    readonly property real optimalHeight: 320
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
    property var rawPositions: Bridge.PositionAccountBridge
        && Bridge.PositionAccountBridge.positions
        ? Bridge.PositionAccountBridge.positions : []

    Connections {
        target: Bridge.PositionAccountBridge
        function onPositionsChanged() {
            rawPositions = Bridge.PositionAccountBridge.positions || []
        }
    }

    // ============ 列定义 ============
    property var columns: [
        { key: "symbol",    label: "代码", baseWidth: 52, priority: 1, visible: true },
        { key: "quantity",  label: "数量", baseWidth: 42, priority: 2, visible: true },
        { key: "pnl",       label: "盈亏", baseWidth: 56, priority: 2, visible: true },
        { key: "lastPrice", label: "现价", baseWidth: 42, priority: 3, visible: true },
        { key: "marketValue",label:"市值", baseWidth: 56, priority: 4, visible: true },
        { key: "pnlRate",   label: "收益率",baseWidth: 46, priority: 4, visible: true },
        { key: "costBasis", label: "成本", baseWidth: 42, priority: 5, visible: false },
        { key: "weight",    label: "占比", baseWidth: 40, priority: 5, visible: false }
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

    // ============ 分组数据 ============
    readonly property var groupDefs: [
        { key: "stock",       title: "股票持仓" },
        { key: "margin_buy",  title: "融资持仓" },
        { key: "margin_sell", title: "融券持仓" },
        { key: "futures",     title: "期货持仓" },
        { key: "options",     title: "期权持仓" }
    ]

    // 分组后的数据: [{ key, title, positions, totalMv, totalPnl, count }]
    readonly property var groupedPositions: {
        var rows = rawPositions.slice()  // 浅拷贝
        // 按市值降序排列
        rows.sort(function(a, b) {
            return (Number(b.marketValue) || 0) - (Number(a.marketValue) || 0)
        })

        var groups = []
        for (var gi = 0; gi < groupDefs.length; gi++) {
            var def = groupDefs[gi]
            var members = []
            var totalMv = 0, totalPnl = 0
            for (var ri = 0; ri < rows.length; ri++) {
                if (String(rows[ri].type || "stock") === def.key) {
                    members.push(rows[ri])
                    totalMv += Number(rows[ri].marketValue || 0)
                    totalPnl += Number(rows[ri].unrealizedPnl || 0)
                }
            }
            if (members.length > 0) {
                groups.push({
                    key: def.key,
                    title: def.title,
                    positions: members,
                    totalMv: totalMv,
                    totalPnl: totalPnl,
                    count: members.length
                })
            }
        }
        return groups
    }

    // Compact 模式: 扁平列表 (所有持仓不分组)
    readonly property var flatPositions: {
        var flat = []
        for (var i = 0; i < groupedPositions.length; i++) {
            flat = flat.concat(groupedPositions[i].positions)
        }
        return flat
    }

    // 合计
    readonly property real totalMarketValue: {
        var sum = 0
        for (var i = 0; i < rawPositions.length; i++) {
            sum += Number(rawPositions[i].marketValue || 0)
        }
        return sum
    }
    readonly property real totalPnl: {
        var s = 0
        for (var j = 0; j < rawPositions.length; j++) {
            s += Number(rawPositions[j].unrealizedPnl || 0)
        }
        return s
    }
    readonly property string totalPnlRate: {
        var cost = totalMarketValue - totalPnl
        if (cost <= 0) return "--"
        return (totalPnl / cost * 100).toFixed(2) + "%"
    }

    // ============ 分组折叠状态 ============
    property var collapsedGroups: ({})  // { groupKey: true } 表示已折叠

    function toggleGroup(key) {
        var next = Object.assign({}, collapsedGroups)
        if (next[key]) {
            delete next[key]
        } else {
            next[key] = true
        }
        collapsedGroups = next
    }

    function isGroupCollapsed(key) {
        // Compact 模式下全部"展开"(扁平)
        if (densityMode === "compact") return false
        return !!collapsedGroups[key]
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
                text: "持仓列表"
                color: Const.tradingTitleText
                font.pixelSize: s(12)
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Text {
                text: "共 " + rawPositions.length + " 条"
                color: Const.tradingEmptyText
                font.pixelSize: s(9)
            }
        }

        Item { Layout.fillHeight: true; Layout.preferredHeight: s(2) }

        // --- 表头 (Normal+) ---
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: s(16)
            spacing: s(2)
            visible: densityMode !== "compact"

            ColH { l: "代码"; pw: s(52); visible: visibleColumn("symbol") }
            ColH { l: "数量"; pw: s(42); visible: visibleColumn("quantity") }
            ColH { l: "盈亏"; pw: s(56); visible: visibleColumn("pnl") }
            ColH { l: "现价"; pw: s(42); visible: visibleColumn("lastPrice") }
            ColH { l: "市值"; pw: s(56); visible: visibleColumn("marketValue") }
            ColH { l: "收益率"; pw: s(46); visible: visibleColumn("pnlRate") }
            ColH { l: "成本"; pw: s(42); visible: visibleColumn("costBasis") }
            ColH { l: "占比"; pw: s(40); visible: visibleColumn("weight") }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: "#334155"
            visible: densityMode !== "compact"
        }

        // --- 内容区 ---
        // Compact: 扁平列表
        ListView {
            id: compactList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: flatPositions
            clip: true
            spacing: 0
            visible: densityMode === "compact"

            delegate: PositionRow {
                width: compactList.width
                pos: modelData
            }
        }

        // Normal/Expanded: 分组列表
        ListView {
            id: groupedList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: groupedPositions
            clip: true
            spacing: s(2)
            visible: densityMode !== "compact"

            delegate: ColumnLayout {
                width: groupedList.width
                spacing: s(1)

                // --- 分组标题 ---
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: s(20)
                    color: "#0d1728"
                    radius: 4

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: s(6)
                        anchors.rightMargin: s(6)
                        spacing: s(4)

                        Text {
                            text: (root.isGroupCollapsed(modelData.key) ? "▶" : "▼") + " " + modelData.title
                            color: Const.tradingLightBlue
                            font.pixelSize: s(10)
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                        }
                        Text {
                            text: "(" + modelData.count + ")  市值 " + Base.fmtAmount(modelData.totalMv)
                                  + "  盈亏 " + (modelData.totalPnl >= 0 ? "+" : "") + Base.fmtAmount(modelData.totalPnl)
                            color: "#64748B"
                            font.pixelSize: s(8)
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.toggleGroup(modelData.key)
                    }
                }

                // --- 分组成员 ---
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: !root.isGroupCollapsed(modelData.key)
                    spacing: 0

                    Repeater {
                        model: modelData.positions
                        PositionRow {
                            Layout.fillWidth: true
                            pos: modelData
                        }
                    }
                }
            }
        }

        // --- 底部汇总条 (Expanded) ---
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: s(18)
            spacing: s(8)
            visible: densityMode === "expanded"

            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#334155"
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: s(18)
            spacing: s(10)
            visible: densityMode === "expanded"

            Text { text: "合计市值: " + Base.fmtAmount(totalMarketValue); color: "#F1F5F9"; font.pixelSize: s(9)
                   font.weight: Font.DemiBold }
            Text { text: "合计盈亏: " + (totalPnl >= 0 ? "+" : "") + Base.fmtAmount(totalPnl)
                       + "  " + (totalPnl >= 0 ? "+" : "") + totalPnlRate
                   color: Base.fmtPnlColor(totalPnl); font.pixelSize: s(9); font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
        }
    }

    // ============ 持仓行组件 ============
    component PositionRow: RowLayout {
        property var pos: ({})
        Layout.fillWidth: true
        Layout.preferredHeight: root.s(22)
        spacing: root.s(2)

        PosText { v: String(pos.symbol || "").replace(/(\d{6}).*/, "$1") || "--"
                  c: "#F1F5F9"; pw: root.s(52); visible: root.visibleColumn("symbol") }
        PosText { v: Base.fmtVolume(pos.quantity)
                  c: "#F1F5F9"; pw: root.s(42); visible: root.visibleColumn("quantity") }
        PosText { v: (Number(pos.unrealizedPnl || 0) >= 0 ? "+" : "") + Base.fmtAmount(pos.unrealizedPnl || 0)
                  c: Base.fmtPnlColor(pos.unrealizedPnl || 0); pw: root.s(56); visible: root.visibleColumn("pnl") }
        PosText { v: Number(pos.lastPrice || 0) > 0 ? Number(pos.lastPrice).toFixed(2) : "--"
                  c: "#F1F5F9"; pw: root.s(42); visible: root.visibleColumn("lastPrice") }
        PosText { v: Base.fmtAmount(pos.marketValue || 0)
                  c: "#94A3B8"; pw: root.s(56); visible: root.visibleColumn("marketValue") }
        PosText { v: pnlRateText(pos)
                  c: Base.fmtPnlColor(pos.unrealizedPnl || 0); pw: root.s(46); visible: root.visibleColumn("pnlRate") }
        PosText { v: Number(pos.costBasis || 0) > 0 ? Number(pos.costBasis).toFixed(2) : "--"
                  c: "#64748B"; pw: root.s(42); visible: root.visibleColumn("costBasis") }
        PosText { v: weightText(pos)
                  c: "#64748B"; pw: root.s(40); visible: root.visibleColumn("weight") }
    }

    component ColH: Text {
        property string l: ""; property real pw: 40
        text: l; color: "#64748B"; font.pixelSize: root.s(8); Layout.preferredWidth: pw
    }

    component PosText: Text {
        property string v: ""; property color c: "#F1F5F9"; property real pw: 40
        text: v; color: c; font.pixelSize: root.s(9); Layout.preferredWidth: pw; elide: Text.ElideRight
    }

    // ============ 辅助函数 ============
    function pnlRateText(pos) {
        var pnl = Number(pos.unrealizedPnl || 0)
        var cost = Number(pos.costBasis || 0) * Number(pos.quantity || 0)
        if (cost <= 0) return "--"
        var rate = pnl / cost * 100
        return (rate >= 0 ? "+" : "") + rate.toFixed(2) + "%"
    }

    function weightText(pos) {
        if (totalMarketValue <= 0) return "--"
        var mv = Number(pos.marketValue || 0)
        return (mv / totalMarketValue * 100).toFixed(1) + "%"
    }
}
