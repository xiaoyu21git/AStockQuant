// CandleTooltip.qml — K线信息浮窗 (独立组件, 可复用)
import QtQuick 2.15

Rectangle {
    id: root
    color: "#0d1117"
    border.color: "#30363d"
    border.width: 1
    radius: 6
    width: 160; height: 120
    visible: false
    z: 200

    property string candleTime: ""
    property double candleOpen: 0
    property double candleHigh: 0
    property double candleLow: 0
    property double candleClose: 0
    property double candleVolume: 0

    function show(x, y, ts, o, h, l, c, v) {
        root.candleTime = new Date(ts).toLocaleString(Qt.locale(), "yyyy-MM-dd hh:mm")
        root.candleOpen = o; root.candleHigh = h; root.candleLow = l
        root.candleClose = c; root.candleVolume = v
        root.x = Math.min(x + 10, parent.width - width - 6)
        root.y = Math.max(0, Math.min(y - height / 2, parent.height - height - 6))
        root.visible = true
    }

    Column {
        anchors.fill: parent; anchors.margins: 8; spacing: 3

        Text { id: dtText; color: "#8b949e"; font.pixelSize: 11 }

        Row { spacing: 12
            Column { Text { text: "开盘"; color: "#8b949e"; font.pixelSize: 10 }
                     Text { id: oText; color: "white"; font.pixelSize: 11 } }
            Column { Text { text: "最高"; color: "#8b949e"; font.pixelSize: 10 }
                     Text { id: hText; color: "#ef5350"; font.pixelSize: 11 } }
        }
        Row { spacing: 12
            Column { Text { text: "最低"; color: "#8b949e"; font.pixelSize: 10 }
                     Text { id: lText; color: "#26a69a"; font.pixelSize: 11 } }
            Column { Text { text: "收盘"; color: "#8b949e"; font.pixelSize: 10 }
                     Text { id: cText; color: "white"; font.pixelSize: 11 } }
        }
        Row { spacing: 12
            Text { text: "成交量"; color: "#8b949e"; font.pixelSize: 10 }
            Text { id: vText; color: "#aaaaaa"; font.pixelSize: 11 }
        }
    }

    // 绑定属性更新
    onCandleTimeChanged:   dtText.text = candleTime
    onCandleOpenChanged:   oText.text = candleOpen.toFixed(2)
    onCandleHighChanged:   hText.text = candleHigh.toFixed(2)
    onCandleLowChanged:    lText.text = candleLow.toFixed(2)
    onCandleCloseChanged:  cText.text = candleClose.toFixed(2)
    onCandleVolumeChanged: vText.text = candleVolume.toLocaleString(Qt.locale(), 'f', 0)
}
