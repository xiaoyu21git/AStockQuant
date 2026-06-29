import QtQuick 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge
import "../../../utils/TradingConstants.js" as Const
import "../../../utils/TradingWidgetBase.js" as Base

// ============================================================================
// AccountCardWidget — 账户概览 (连续卡片自适应)
//
// 核心思路:
//   1. 定义指标池 (按优先级排序), 每个指标有 label / value / color
//   2. 根据宽高计算卡片最小尺寸 → 算出 cols × rows → 选前 N 个指标
//   3. 卡片平均填满可用空间, 无固定密度模式跳变
//
//   卡片最小尺寸: minW=80, minH=40  (scaleFactor=1 时)
//   极矮模式 (< 50px): 单行横排纯文本, 无卡片
//
// Bridges: PositionAccountBridge + TradeExecutionBridge
// ============================================================================

Item {
    id: root
    property var widgetConfig: ({})
    clip: true

    // ============ 连续布局参数 ============
    readonly property int titleH: 24       // 标题栏高度
    readonly property int gap: 4           // 卡片间距
    readonly property int outerPad: 6      // 外边距
    readonly property int minCardW: 80     // 单卡片最小宽度
    readonly property int minCardH: 40     // 单卡片最小高度
    readonly property int idealCardW: 130  // 单卡片理想宽度
    readonly property int idealCardH: 58   // 单卡片理想高度

    // 可用空间
    readonly property int availW: Math.max(0, width - outerPad * 2)
    readonly property int availH: Math.max(0, height - titleH - outerPad * 2 - gap)

    // 根据宽度算列数: 优先按 ideal 算, 放不下就按 min 算
    readonly property int cols: {
        if (availW <= 0) return 1
        var byIdeal = Math.floor(availW / idealCardW)
        if (byIdeal >= 1) return byIdeal
        return Math.max(1, Math.floor(availW / minCardW))
    }

    // 根据高度算行数
    readonly property int rows: {
        if (availH <= 0) return 1
        return Math.max(1, Math.floor(availH / minCardH))
    }

    // 最多容纳卡片数
    readonly property int maxCards: cols * rows

    // 实际卡片宽高 (均匀分配可用空间)
    readonly property real actualCardW: cols > 0 ? (availW - (cols - 1) * gap) / cols : availW
    readonly property real actualCardH: rows > 0 ? (availH - (rows - 1) * gap) / rows : availH

    // 极小高度 → 文本行模式
    readonly property bool tinyMode: height < 50

    // 字体缩放: 基于卡片高度
    readonly property real cardScale: Math.min(1.0, Math.max(0.55, actualCardH / 50.0))
    function cs(v) { return Math.max(1, Math.round(v * cardScale)) }

    // ============ Bridge 数据 (直连, 不经过中间变量) ============
    property var accountSnapshot: Bridge.PositionAccountBridge
        && Bridge.PositionAccountBridge.accountSnapshot
        ? Bridge.PositionAccountBridge.accountSnapshot : ({})
    property var positions: Bridge.PositionAccountBridge
        && Bridge.PositionAccountBridge.positions
        ? Bridge.PositionAccountBridge.positions : []
    property var orders: Bridge.TradeExecutionBridge
        && Bridge.TradeExecutionBridge.recentOrders
        ? Bridge.TradeExecutionBridge.recentOrders : []

    Connections {
        target: Bridge.PositionAccountBridge
        function onAccountSnapshotChanged() {
            accountSnapshot = Bridge.PositionAccountBridge.accountSnapshot || ({})
        }
        function onPositionsChanged() {
            positions = Bridge.PositionAccountBridge.positions || []
        }
    }
    Connections {
        target: Bridge.TradeExecutionBridge
        function onRecentOrdersChanged() {
            orders = Bridge.TradeExecutionBridge.recentOrders || []
        }
    }

    // ============ 指标数据 ============
    readonly property real totalAsset: accountSnapshot.totalAsset !== undefined
        ? Number(accountSnapshot.totalAsset) : (availableCash + marketValue)
    readonly property real marketValue: accountSnapshot.marketValue !== undefined
        ? Number(accountSnapshot.marketValue) : sumPositionMv()
    readonly property real availableCash: accountSnapshot.availableCash !== undefined
        ? Number(accountSnapshot.availableCash) : 0
    readonly property real unrealizedPnl: accountSnapshot.unrealizedPnl !== undefined
        ? Number(accountSnapshot.unrealizedPnl) : 0
    readonly property real varUsagePct: Bridge.RiskControlBridge
        ? Number(Bridge.RiskControlBridge.varUsagePercent || 0) : 0

    readonly property int filledCount: {
        var c = 0
        for (var i = 0; i < orders.length; i++) {
            if (String(orders[i].rawStatus || orders[i].status || "") === "FILLED") c++
        }
        return c
    }
    readonly property int rejectedCount: {
        var c = 0
        for (var j = 0; j < orders.length; j++) {
            if (String(orders[j].rawStatus || orders[j].status || "") === "REJECTED") c++
        }
        return c
    }
    readonly property real exposurePct: totalAsset > 0 ? marketValue / totalAsset * 100 : 0

    function sumPositionMv() {
        var s = 0
        for (var i = 0; i < positions.length; i++) s += Number(positions[i].marketValue || 0)
        return s
    }

    // =========================================================================
    // 指标池 (合并原 AccountCard + TradingOverview 全部指标)
    // 按 priority 排序, 宽高不够时自动裁剪低优先级卡片
    // priority: 1=最高, 5=最低
    // =========================================================================
    property var cardPool: [
        { label: "总资产",   value: Base.fmtAmount(totalAsset),
          color: "#F1F5F9", priority: 1,
          sub: totalAsset >= 1e4 ? (totalAsset / 1e4).toFixed(0) + "万" : "" },
        { label: "可用资金", value: Base.fmtAmount(availableCash),
          color: "#F1F5F9", priority: 1,
          sub: exposurePct.toFixed(0) + "% 持仓" },
        { label: "持仓市值", value: Base.fmtAmount(marketValue),
          color: "#F1F5F9", priority: 2,
          sub: positions.length + " 只品种" },
        { label: "浮动盈亏", value: (unrealizedPnl >= 0 ? "+" : "") + Base.fmtAmount(unrealizedPnl),
          color: Base.fmtPnlColor(unrealizedPnl), priority: 2,
          sub: totalAsset > 0 ? (unrealizedPnl/totalAsset*100).toFixed(2) + "%" : "" },
        { label: "风险敞口", value: exposurePct.toFixed(1) + "%",
          color: exposurePct > 80 ? "#F59E0B" : "#F1F5F9", priority: 3,
          sub: "市值/总资产" },
        { label: "VaR使用",  value: varUsagePct.toFixed(1) + "%",
          color: varUsagePct > 50 ? "#F59E0B" : "#F1F5F9", priority: 3,
          sub: "在险价值占比" },
        { label: "今日委托", value: String(orders.length) + " 笔",
          color: "#F1F5F9", priority: 4,
          sub: "已成" + filledCount },
        { label: "委托拒单", value: String(rejectedCount) + " 笔",
          color: rejectedCount > 0 ? "#EF4444" : "#94A3B8", priority: 4,
          sub: orders.length > 0 ? "拒单率 " + (rejectedCount/orders.length*100).toFixed(0) + "%" : "" },
        { label: "今日成交", value: String(filledCount) + " 笔",
          color: "#10B981", priority: 5,
          sub: orders.length > 0 ? "成交率 " + (filledCount/orders.length*100).toFixed(0) + "%" : "" },
        { label: "持仓品种", value: String(positions.length) + " 只",
          color: "#F1F5F9", priority: 5,
          sub: marketValue >= 1e4 ? "市值 " + (marketValue/1e4).toFixed(1) + "万" : "" }
    ]

    // 可见卡片: 按 priority 排序后取前 maxCards 个
    readonly property var visibleCards: {
        var pool = cardPool.slice()
        pool.sort(function(a, b) { return (a.priority || 99) - (b.priority || 99) })
        var n = Math.min(pool.length, maxCards)
        if (n <= 0) return []
        return pool.slice(0, n)
    }

    // 实际使用的行数 (可能少于 rows)
    readonly property int actualRows: Math.max(1, Math.ceil(visibleCards.length / cols))

    // ============ 布局 ============
    // 极矮模式: 单行横排纯文本
    Loader {
        anchors.fill: parent
        anchors.margins: 4
        active: tinyMode
        sourceComponent: tinyRowComponent
    }

    // 卡片模式
    Loader {
        anchors.fill: parent
        active: !tinyMode
        sourceComponent: cardGridComponent
    }

    // ============ 极矮文本行组件 ============
    Component {
        id: tinyRowComponent
        RowLayout {
            anchors.fill: parent
            spacing: 4
            Repeater {
                model: cardPool.slice(0, Math.min(cardPool.length, Math.max(3, Math.floor(availW / 90))))
                RowLayout {
                    spacing: 1
                    Text {
                        text: modelData.label.substring(0, 2)
                        color: "#64748B"
                        font.pixelSize: Math.max(7, Math.round(root.height * 0.25))
                    }
                    Text {
                        text: modelData.value
                        color: modelData.color
                        font.pixelSize: Math.max(8, Math.round(root.height * 0.28))
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }
    }

    // ============ 卡片网格组件 ============
    Component {
        id: cardGridComponent
        Item {
            anchors.fill: parent

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: outerPad
                spacing: gap

                // 标题
                Text {
                    Layout.fillWidth: true
                    Layout.preferredHeight: titleH
                    text: "账户概览"
                    color: Const.tradingTitleText
                    font.pixelSize: cs(13)
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignBottom
                }

                // 卡片区域
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Grid {
                        anchors.fill: parent
                        columns: cols
                        spacing: gap

                        Repeater {
                            model: visibleCards

                            Rectangle {
                                width: actualCardW
                                height: actualCardH
                                radius: cs(5)
                                color: "#0d1728"
                                border.color: "#21354c"
                                border.width: 1

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: cs(6)
                                    spacing: Math.max(0, cs(2))

                                    Text {
                                        text: modelData.label
                                        color: "#64748B"
                                        font.pixelSize: cs(10)
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: modelData.value
                                        color: modelData.color
                                        font.pixelSize: cs(15)
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: modelData.sub || ""
                                        color: "#4a6a8a"
                                        font.pixelSize: cs(8)
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                        visible: modelData.sub && modelData.sub.length > 0
                                               && actualCardH >= 52
                                    }
                                    Item { Layout.fillHeight: true; visible: actualCardH < 52 }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
