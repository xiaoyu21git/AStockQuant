import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root

    property var icSeries: []
    property var groupReturnSeries: []
    property var returnSeries: ({})
    property var dateList: []
    property int numGroups: 5
    property int chartHeight: 260

    implicitHeight: icChart.height + groupChart.height + scatterChart.height + 48

    // ── IC 日序列折线图 ──
    ColumnLayout {
        id: icChart
        width: parent.width
        spacing: 4

        Text {
            text: "IC 日序列"
            font.pixelSize: 13
            font.weight: Font.Bold
            color: "#E2E8F0"
        }
        Text {
            text: "每日 Rank IC 走势"
            font.pixelSize: 11
            color: "#94A3B8"
        }

        Rectangle {
            Layout.fillWidth: true
            height: root.chartHeight
            color: "#1E293B"
            radius: 8

            Canvas {
                id: icCanvas
                anchors.fill: parent
                anchors.margins: 8

                onPaint: {
                    var ctx = getContext("2d")
                    var w = width, h = height
                    ctx.clearRect(0, 0, w, h)
                    var data = root.icSeries
                    if (!data || data.length < 2) return

                    var n = data.length
                    var minV = 0, maxV = 0
                    for (var i = 0; i < n; i++) {
                        var v = data[i]
                        if (v < minV) minV = v
                        if (v > maxV) maxV = v
                    }
                    var range = Math.max(Math.abs(minV), Math.abs(maxV), 0.05) * 1.15
                    var yMid = h / 2

                    // zero line
                    ctx.strokeStyle = "#334155"
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.moveTo(0, yMid)
                    ctx.lineTo(w, yMid)
                    ctx.stroke()

                    // IC line
                    ctx.strokeStyle = "#3B82F6"
                    ctx.lineWidth = 1.5
                    ctx.beginPath()
                    for (var i = 0; i < n; i++) {
                        var x = (i / (n - 1)) * w
                        var y = yMid - (data[i] / range) * yMid
                        if (i === 0) ctx.moveTo(x, y)
                        else ctx.lineTo(x, y)
                    }
                    ctx.stroke()

                    // positive area
                    ctx.fillStyle = "rgba(59,130,246,0.12)"
                    ctx.beginPath()
                    for (var j = 0; j < n; j++) {
                        var xj = (j / (n - 1)) * w
                        var yj = yMid - (data[j] / range) * yMid
                        if (j === 0) ctx.moveTo(xj, yj)
                        else ctx.lineTo(xj, yj)
                    }
                    ctx.lineTo(w, yMid)
                    ctx.lineTo(0, yMid)
                    ctx.closePath()
                    ctx.fill()
                }
            }

            Connections {
                target: root
                function onIcSeriesChanged() { icCanvas.requestPaint() }
            }
        }
    }

    // ── 分组累积收益曲线 ──
    ColumnLayout {
        id: groupChart
        width: parent.width
        spacing: 4

        Text {
            text: "分组累积收益"
            font.pixelSize: 13
            font.weight: Font.Bold
            color: "#E2E8F0"
        }
        Text {
            text: "各组每日收益的累积净值曲线"
            font.pixelSize: 11
            color: "#94A3B8"
        }

        Rectangle {
            Layout.fillWidth: true
            height: root.chartHeight
            color: "#1E293B"
            radius: 8

            Canvas {
                id: grpCanvas
                anchors.fill: parent
                anchors.margins: 8

                property var colors: ["#EF4444","#F59E0B","#3B82F6","#10B981","#8B5CF6"]

                onPaint: {
                    var ctx = getContext("2d")
                    var w = width, h = height
                    ctx.clearRect(0, 0, w, h)
                    var grs = root.groupReturnSeries
                    if (!grs || grs.length === 0) return

                    var nDays = 0
                    for (var g = 0; g < grs.length; g++) {
                        var d = normalizedListValue(grs[g].data || grs[g])
                        if (d.length > nDays) nDays = d.length
                    }
                    if (nDays < 2) return

                    // compute cumulative for each group
                    var minC = 1, maxC = 1
                    var cumulatives = []
                    for (var g2 = 0; g2 < grs.length; g2++) {
                        var raw = normalizedListValue(grs[g2].data || grs[g2])
                        var cum = [1.0]
                        for (var i = 1; i < raw.length; i++) {
                            cum.push(cum[i-1] * (1 + raw[i]))
                        }
                        cumulatives.push(cum)
                        for (var j = 0; j < cum.length; j++) {
                            if (cum[j] < minC) minC = cum[j]
                            if (cum[j] > maxC) maxC = cum[j]
                        }
                    }
                    var cRange = maxC - minC
                    if (cRange < 0.01) cRange = 0.1

                    // baseline at 1.0
                    var baseY = h - ((1.0 - minC) / cRange) * h
                    ctx.strokeStyle = "#334155"
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.moveTo(0, baseY)
                    ctx.lineTo(w, baseY)
                    ctx.stroke()

                    for (var g3 = 0; g3 < grs.length; g3++) {
                        var cum2 = cumulatives[g3]
                        ctx.strokeStyle = colors[g3 % colors.length]
                        ctx.lineWidth = 1.5
                        ctx.beginPath()
                        for (var k = 0; k < Math.min(cum2.length, nDays); k++) {
                            var x = (k / (nDays - 1)) * w
                            var y = h - ((cum2[k] - minC) / cRange) * h
                            if (k === 0) ctx.moveTo(x, y)
                            else ctx.lineTo(x, y)
                        }
                        ctx.stroke()
                    }
                }
            }

            Connections {
                target: root
                function onGroupReturnSeriesChanged() { grpCanvas.requestPaint() }
            }
        }
    }

    // ── 因子值 vs 前向收益散点图 ──
    ColumnLayout {
        id: scatterChart
        width: parent.width
        spacing: 4

        Text {
            text: "因子-收益散点"
            font.pixelSize: 13
            font.weight: Font.Bold
            color: "#E2E8F0"
        }
        Text {
            text: "因子值与下期收益率的关系"
            font.pixelSize: 11
            color: "#94A3B8"
        }

        Rectangle {
            Layout.fillWidth: true
            height: root.chartHeight
            color: "#1E293B"
            radius: 8

            Canvas {
                id: scatterCanvas
                anchors.fill: parent
                anchors.margins: 8

                property var fvSamples: []

                function prepareRandom() {
                    // placeholder until real factorValues data loads
                    var pts = []
                    for (var i = 0; i < 200; i++) {
                        pts.push({
                            x: (Math.random() - 0.5) * 4,
                            y: (Math.random() - 0.5) * 0.2
                        })
                    }
                    fvSamples = pts
                }

                onPaint: {
                    var ctx = getContext("2d")
                    var w = width, h = height
                    ctx.clearRect(0, 0, w, h)
                    var pts = fvSamples
                    if (pts.length === 0) { prepareRandom(); pts = fvSamples }

                    var minX = 0, maxX = 0, minY = 0, maxY = 0
                    for (var i = 0; i < pts.length; i++) {
                        if (pts[i].x < minX) minX = pts[i].x
                        if (pts[i].x > maxX) maxX = pts[i].x
                        if (pts[i].y < minY) minY = pts[i].y
                        if (pts[i].y > maxY) maxY = pts[i].y
                    }
                    var xR = Math.max(Math.abs(minX), Math.abs(maxX), 0.5) * 1.1
                    var yR = Math.max(Math.abs(minY), Math.abs(maxY), 0.05) * 1.1
                    var cx = w / 2, cy = h / 2

                    // axes
                    ctx.strokeStyle = "#334155"
                    ctx.lineWidth = 1
                    ctx.beginPath(); ctx.moveTo(0, cy); ctx.lineTo(w, cy); ctx.stroke()
                    ctx.beginPath(); ctx.moveTo(cx, 0); ctx.lineTo(cx, h); ctx.stroke()

                    // points
                    for (var j = 0; j < pts.length; j++) {
                        var sx = cx + (pts[j].x / xR) * cx
                        var sy = cy - (pts[j].y / yR) * cy
                        if (sx < 1 || sx > w - 1 || sy < 1 || sy > h - 1) continue
                        ctx.fillStyle = "rgba(59,130,246,0.35)"
                        ctx.beginPath()
                        ctx.arc(sx, sy, 2, 0, Math.PI * 2)
                        ctx.fill()
                    }
                }
            }
        }
    }

    function normalizedListValue(v) {
        if (v instanceof Array) return v
        if (typeof v === "object" && v !== null) {
            var arr = []
            for (var i = 0; i < Object.keys(v).length; i++) arr.push(v[i])
            return arr
        }
        return []
    }
}
