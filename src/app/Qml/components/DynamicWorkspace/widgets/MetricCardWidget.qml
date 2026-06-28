import QtQuick 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge

Rectangle {
    id: root
    property var widgetConfig: ({})
    color: "transparent"

    readonly property var acct: Bridge.PositionAccountBridge.accountSnapshot || ({})

    function fmt(v) {
        if (v === undefined || v === null) return "--"
        var n = Number(v)
        if (Math.abs(n) >= 1e8) return (n/1e8).toFixed(2) + "亿"
        if (Math.abs(n) >= 1e4) return (n/1e4).toFixed(2) + "万"
        return n.toFixed(2)
    }

    readonly property var metricMap: ({
        "totalAsset":        { label: "总资产",   value: fmt(acct.totalAsset) },
        "marketValue":       { label: "持仓市值", value: fmt(acct.marketValue) },
        "availableCash":     { label: "可用资金", value: fmt(acct.availableCash) },
        "unrealizedPnl":     { label: "浮动盈亏", value: fmt(acct.unrealizedPnl),
                               isPnl: true },
        "realizedPnl":       { label: "已实现盈亏", value: fmt(acct.realizedPnl || 0),
                               isPnl: true }
    })

    readonly property var display: {
        var key = widgetConfig.metricKey || "totalAsset"
        return metricMap[key] || { label: widgetConfig.label || "指标", value: "--" }
    }

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 4

        Text {
            text: display.label
            color: "#94A3B8"
            font.pixelSize: 12
            Layout.alignment: Qt.AlignHCenter
        }

        Text {
            text: display.value
            color: {
                if (!display.isPnl) return "#F1F5F9"
                var n = Number(acct[widgetConfig.metricKey || "totalAsset"] || 0)
                return n >= 0 ? "#10B981" : "#EF4444"
            }
            font.pixelSize: 28
            font.weight: Font.Bold
            Layout.alignment: Qt.AlignHCenter
        }
    }
}
