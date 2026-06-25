import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../Base/Constants.qml" as Const

// ── TradePanel — 交易下单组件入口 ──
// displayDensity: "mini" | "compact" | "full"
// 通过属性注入获取所有数据，不主动拉取

Rectangle {
    id: root

    // ══════════════════════════════════════
    // 输入属性
    // ══════════════════════════════════════
    property string symbol: ""
    property var marketSnapshot: null
    property var pendingOrders: []
    property string displayDensity: "auto"   // "mini" | "compact" | "full" | "auto"
    property bool showDepth: false

    // auto 模式下根据宽度自动选择密度
    readonly property string effectiveDensity: {
        if (displayDensity !== "auto") return displayDensity
        var w = root.width
        if (w < 280) return "mini"
        if (w < 500) return "compact"
        return "full"
    }

    // 快捷填单依赖（P2 补全）
    property var accountSnapshot: null
    property var feeRate: ({ commission: 0.0003, stampTax: 0.001, minCommission: 5.0 })

    // 错误反馈
    property string lastError: ""

    // 内部状态
    property string priceType: "market"
    property double orderPrice: 0.0
    property int orderQuantity: 0
    property string orderSide: "buy"

    // ══════════════════════════════════════
    // 输出信号
    // ══════════════════════════════════════
    signal orderRequested(var payload)
    signal cancelRequested(string orderId)

    // ══════════════════════════════════════
    // 布局
    // ══════════════════════════════════════
    implicitWidth: 240
    implicitHeight: contentColumn.implicitHeight + 16
    color: Const.Constants.secondaryBg
    radius: 10

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // ── 代码 + 现价 ──
        TradeSymbolBar {
            id: symbolBar
            Layout.fillWidth: true
            symbol: root.symbol
            marketSnapshot: root.marketSnapshot
            displayDensity: root.effectiveDensity

            onSymbolChanged: function(s) {
                root.symbol = s
                // 切换标的后重置下单参数
                root.orderPrice = 0.0
                root.orderQuantity = 0
                root.priceType = "market"
                root.lastError = ""
            }
        }

        // ── 订单表单（compact/full 模式）──
        TradeOrderSheet {
            id: orderSheet
            visible: root.effectiveDensity !== "mini"
            Layout.fillWidth: true
            priceType: root.priceType
            orderPrice: root.orderPrice
            orderQuantity: root.orderQuantity
            orderSide: root.orderSide
            displayDensity: root.effectiveDensity
            marketSnapshot: root.marketSnapshot
            accountSnapshot: root.accountSnapshot
            feeRate: root.feeRate

            onPriceTypeChanged: root.priceType = orderSheet.priceType
            onOrderPriceChanged: root.orderPrice = orderSheet.orderPrice
            onOrderQuantityChanged: root.orderQuantity = orderSheet.orderQuantity
        }

        // ── 买卖按钮 ──
        TradeQuickActions {
            id: quickActions
            Layout.fillWidth: true
            displayDensity: root.effectiveDensity
            canSubmit: root.symbol !== ""
            lastError: root.lastError

            onBuyRequested: {
                root.orderSide = "buy"
                root.lastError = ""
                root.orderRequested({
                    side: "buy",
                    priceType: root.priceType,
                    price: root.priceType === "limit" ? root.orderPrice : 0,
                    quantity: root.orderQuantity > 0 ? root.orderQuantity : 100
                })
                // 下单后重置
                root.orderPrice = 0.0
                root.orderQuantity = 0
                root.priceType = "market"
            }

            onSellRequested: {
                root.orderSide = "sell"
                root.lastError = ""
                root.orderRequested({
                    side: "sell",
                    priceType: root.priceType,
                    price: root.priceType === "limit" ? root.orderPrice : 0,
                    quantity: root.orderQuantity > 0 ? root.orderQuantity : 100
                })
                root.orderPrice = 0.0
                root.orderQuantity = 0
                root.priceType = "market"
            }
        }

        // ── 委托队列（full 模式）──
        TradePendingList {
            id: pendingList
            visible: root.effectiveDensity === "full"
            Layout.fillWidth: true
            orders: root.pendingOrders
            onCancelRequested: function(id) { root.cancelRequested(id) }
        }

        // ── 盘口五档（可选扩展）──
        Loader {
            id: depthLoader
            visible: root.showDepth && root.effectiveDensity === "full"
            active: root.showDepth
            Layout.fillWidth: true
            Layout.preferredHeight: item ? item.implicitHeight : 0
            sourceComponent: undefined  // P5 实现 TradeDepthPanel
        }
    }

    // ── 错误自动清除定时器 ──
    Timer {
        id: errorTimer
        interval: 3000
        running: root.lastError !== ""
        onTriggered: root.lastError = ""
    }
}
