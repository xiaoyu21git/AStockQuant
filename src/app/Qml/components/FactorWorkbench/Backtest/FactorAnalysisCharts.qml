import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    property var icSeries: []
    property var groupReturnSeries: []
    property var returnSeries: ({})
    property int numGroups: 5
    property int chartHeight: 260
    property var scatterData: []

    implicitHeight: 600

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Text { text: "IC Chart"; color: "#E2E8F0"; font.pixelSize: 14 }
        Rectangle {
            Layout.fillWidth: true; height: 200; color: "#1E293B"; radius: 8
            Canvas {
                id: icCanvas; anchors.fill: parent; anchors.margins: 8
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    var data = root.icSeries
                    if (!data || data.length < 2) return
                    var n = data.length
                    var maxV = 0.05
                    for (var i = 0; i < n; i++) {
                        var v = data[i]; if (v < 0) v = -v
                        if (v > maxV) maxV = v
                    }
                    maxV = maxV * 1.15
                    var mid = height / 2
                    ctx.strokeStyle = "#334155"; ctx.lineWidth = 1
                    ctx.beginPath(); ctx.moveTo(0, mid); ctx.lineTo(width, mid); ctx.stroke()
                    ctx.strokeStyle = "#3B82F6"; ctx.lineWidth = 1.5
                    ctx.beginPath()
                    for (var j = 0; j < n; j++) {
                        var x = (j / (n - 1)) * width
                        var y = mid - (data[j] / maxV) * mid
                        if (j === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                    }
                    ctx.stroke()
                }
            }
        }

        Text { text: "Group Chart"; color: "#E2E8F0"; font.pixelSize: 14 }
        Rectangle {
            Layout.fillWidth: true; height: 200; color: "#1E293B"; radius: 8
            Canvas {
                id: grpCanvas; anchors.fill: parent; anchors.margins: 8
                property var colors: ["#EF4444","#F59E0B","#3B82F6","#10B981","#8B5CF6"]
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    var grs = root.groupReturnSeries
                    if (!grs || grs.length === 0) return
                    var nDays = 0
                    for (var g = 0; g < grs.length; g++) {
                        var d = grs[g].data || grs[g]
                        if (d && d.length > nDays) nDays = d.length
                    }
                    if (nDays < 2) return
                    var baseY = height * 0.85
                    ctx.strokeStyle = "#334155"; ctx.lineWidth = 1
                    ctx.beginPath(); ctx.moveTo(0, baseY); ctx.lineTo(width, baseY); ctx.stroke()
                    for (var g2 = 0; g2 < grs.length; g2++) {
                        var raw = grs[g2].data || grs[g2]
                        if (!raw || raw.length < 2) continue
                        var cum = [1.0]
                        for (var i = 1; i < raw.length; i++) cum.push(cum[i-1] * (1 + raw[i]))
                        ctx.strokeStyle = colors[g2 % colors.length]; ctx.lineWidth = 1.5
                        ctx.beginPath()
                        for (var k = 0; k < cum.length; k++) {
                            var x = (k / (nDays - 1)) * width
                            var y = baseY - (cum[k] - 1.0) * height * 0.5
                            if (k === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                        }
                        ctx.stroke()
                    }
                }
            }
        }

        Text { text: "Scatter"; color: "#E2E8F0"; font.pixelSize: 14 }
        Rectangle {
            Layout.fillWidth: true; height: 200; color: "#1E293B"; radius: 8
            Canvas {
                id: scatterCanvas; anchors.fill: parent; anchors.margins: 8
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    var data = root.scatterData
                    if (!data || data.length < 2) return
                    var minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9
                    for (var i = 0; i < data.length; i++) {
                        var fv = data[i].factorValue || 0
                        var fr = data[i].forwardRet || 0
                        if (fv < minX) minX = fv; if (fv > maxX) maxX = fv
                        if (fr < minY) minY = fr; if (fr > maxY) maxY = fr
                    }
                    if (maxX === minX) { minX -= 0.5; maxX += 0.5 }
                    if (maxY === minY) { minY -= 0.05; maxY += 0.05 }
                    var xR = maxX - minX; var yR = maxY - minY
                    var midX = width / 2; var midY = height / 2
                    ctx.strokeStyle = "#334155"; ctx.lineWidth = 1
                    ctx.beginPath(); ctx.moveTo(0, midY); ctx.lineTo(width, midY); ctx.stroke()
                    ctx.beginPath(); ctx.moveTo(midX, 0); ctx.lineTo(midX, height); ctx.stroke()
                    for (var j = 0; j < data.length; j++) {
                        var fv2 = data[j].factorValue || 0
                        var fr2 = data[j].forwardRet || 0
                        var sx = midX + ((fv2 - (minX + maxX) / 2) / (xR || 1)) * width * 0.8
                        var sy = midY - ((fr2 - (minY + maxY) / 2) / (yR || 1)) * height * 0.8
                        if (sx < 1 || sx > width - 1 || sy < 1 || sy > height - 1) continue
                        ctx.fillStyle = "rgba(59,130,246,0.3)"
                        ctx.beginPath(); ctx.arc(sx, sy, 1.5, 0, Math.PI * 2); ctx.fill()
                    }
                }
            }
        }
    }

    function normalizedListValue(v) {
        if (v instanceof Array) return v
        if (typeof v === "object" && v !== null) {
            var arr = []; for (var i = 0; i < Object.keys(v).length; i++) arr.push(v[i])
            return arr
        }
        return []
    }
}
