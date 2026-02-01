import QtQuick
import QtQuick.Controls

Item {
    id: root
    property var model
    // 触发重绘用的计数属性：使用模型的 rowCount()
    property int modelCount: model ? model.rowCount() : 0

    // 线条和背景样式
    property color lineColor: "#4CAF50"
    property real lineWidth: 2
    property color gridColor: "#404040"
    property color backgroundColor: "transparent"

    Rectangle {
        anchors.fill: parent
        color: backgroundColor

        Canvas {
            id: canvas
            anchors.fill: parent

            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()

                var w = width
                var h = height

                if (!root.model || root.modelCount < 2 || w <= 0 || h <= 0)
                    return

                // 计算最小/最大权益
                var minY = Number.POSITIVE_INFINITY
                var maxY = Number.NEGATIVE_INFINITY

                for (var i = 0; i < root.modelCount; ++i) {
                    var row = root.model.get(i)
                    if (!row)
                        continue
                    var v = Number(row.equity)
                    if (isNaN(v))
                        continue
                    if (v < minY) minY = v
                    if (v > maxY) maxY = v
                }

                if (!isFinite(minY) || !isFinite(maxY))
                    return

                if (maxY === minY) {
                    maxY = minY + 1.0
                }

                var paddingTop = 8
                var paddingBottom = 16
                var usableH = h - paddingTop - paddingBottom
                if (usableH <= 0)
                    usableH = h

                // 简单画一条基准线
                ctx.strokeStyle = gridColor
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.moveTo(0, h - paddingBottom)
                ctx.lineTo(w, h - paddingBottom)
                ctx.stroke()

                // 画折线
                var count = root.modelCount
                var dx = (count > 1) ? (w / (count - 1)) : w

                ctx.strokeStyle = lineColor
                ctx.lineWidth = lineWidth
                ctx.beginPath()

                var first = true
                for (var j = 0; j < count; ++j) {
                    var r = root.model.get(j)
                    if (!r)
                        continue
                    var yv = Number(r.equity)
                    if (isNaN(yv))
                        continue

                    var x = dx * j
                    var t = (yv - minY) / (maxY - minY)
                    var y = paddingTop + (1.0 - t) * usableH

                    if (first) {
                        ctx.moveTo(x, y)
                        first = false
                    } else {
                        ctx.lineTo(x, y)
                    }
                }

                ctx.stroke()
            }

            Component.onCompleted: requestPaint()
        }
    }

    onModelCountChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
}
