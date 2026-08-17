import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15

// ═══ 因子归因面板 (因子收益法, 组合因子模式) ═══
// 数据源: metricSections.factorAttribution (Orchestrator 标量预聚合 → FactorAttributionCalculator)
// 恒等式: 组合总收益 = Σ因子贡献 + 排名交互残差 + 成本残差
// 单因子模式不产出该键 → 页面侧控制本面板不可见, 改显多空收益小卡
Rectangle {
    id: root
    radius: 8
    color: "#1E293B"
    Layout.fillWidth: true
    Layout.preferredHeight: contentCol.implicitHeight + 16

    property var report: null
    property bool valid: report ? (report.isValid === true) : false
    property var rows: valid && report.rows ? report.rows : []
    property var colorPalette: ["#EF4444", "#F59E0B", "#3B82F6", "#10B981", "#8B5CF6"]

    // 累计曲线长度 (子因子期序列等长, 取首行)
    property int curveLength: {
        if (valid && rows.length > 0 && rows[0].cumulativeContribution)
            return rows[0].cumulativeContribution.length
        return 0
    }

    // ── 格式化工具 ──
    function fp(v, d) {
        var n = Number(v)
        return isNaN(n) ? "--" : (n * 100).toFixed(d === undefined ? 2 : d) + "%"
    }
    function pnlColor(v) {
        var n = Number(v)
        return isNaN(n) ? "#94A3B8" : (n >= 0 ? "#EF4444" : "#10B981")
    }
    function maxAbsContrib() {
        var m = 0
        for (var i = 0; i < rows.length; i++) {
            var c = Math.abs(Number(rows[i].contribution) || 0)
            if (c > m) m = c
        }
        return m
    }
    function rowName(row) {
        return row && (row.factorName || row.factorId) ? (row.factorName || row.factorId) : "--"
    }

    onReportChanged: refreshStacked()

    // ── 堆叠面积图: 每因子累计贡献 + 组合实际累计虚线 ──
    function refreshStacked() {
        stackChart.removeAllSeries()
        if (!valid || curveLength === 0) return
        var i, k
        for (i = 0; i < rows.length; i++) {
            var row = rows[i]
            if (!row.cumulativeContribution || row.cumulativeContribution.length === 0) continue
            var area = stackChart.createSeries(ChartView.SeriesTypeArea, rowName(row), stackX, stackY)
            var c = colorPalette[i % colorPalette.length]
            area.color = c
            area.borderColor = c
            area.borderWidth = 1
            for (k = 0; k < row.cumulativeContribution.length; k++)
                area.append(k, Number(row.cumulativeContribution[k]))
        }
        if (report.compositeCumulative && report.compositeCumulative.length > 0) {
            var totalLine = stackChart.createSeries(ChartView.SeriesTypeLine, "组合实际(扣费后)", stackX, stackY)
            totalLine.color = "#F1F5F9"
            totalLine.style = Qt.DashLine
            totalLine.width = 2
            for (var t = 0; t < report.compositeCumulative.length; t++)
                totalLine.append(t, Number(report.compositeCumulative[t]))
        }
        stackX.max = Math.max(1, curveLength - 1)
    }

    ColumnLayout {
        id: contentCol
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // 标题行
        RowLayout {
            Layout.fillWidth: true
            Text { text: "因子归因（因子收益法）"; font.pixelSize: 13; font.weight: Font.Bold; color: "#F1F5F9" }
            Item { Layout.fillWidth: true }
            Text {
                visible: valid
                text: "总多空 " + fp(report.totalLongShortReturn, 2)
                font.pixelSize: 10
                font.weight: Font.Bold
                color: pnlColor(report.totalLongShortReturn)
            }
        }

        // 口径小字
        Text {
            Layout.fillWidth: true
            visible: report && (report.notice || "")
            text: report ? (report.notice || "") : ""
            font.pixelSize: 9
            color: "#64748B"
            wrapMode: Text.WordWrap
        }

        // 逐因子表: 因子名 | 权重 | IC | ICIR | 多空累计收益 | 收益贡献 | 覆盖天数
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(30 + rows.length * 26 + 14, 300)
            visible: valid
            color: "#0B1220"
            radius: 6
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 2
                Row {
                    Layout.fillWidth: true
                    height: 18
                    Text { width: 130; text: "因子"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; leftPadding: 4 }
                    Text { width: 52; text: "权重"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; horizontalAlignment: Text.AlignRight }
                    Text { width: 48; text: "IC"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; horizontalAlignment: Text.AlignRight }
                    Text { width: 48; text: "ICIR"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; horizontalAlignment: Text.AlignRight }
                    Text { width: 86; text: "多空累计"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; horizontalAlignment: Text.AlignRight }
                    Item {
                        width: 150; height: parent.height
                        Text { anchors.centerIn: parent; text: "收益贡献"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold }
                    }
                    Text { width: 56; text: "覆盖天数"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; horizontalAlignment: Text.AlignRight }
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: rows
                    delegate: factorRowDelegate
                }
            }
        }

        // 残差双行 (灰色)
        ColumnLayout {
            Layout.fillWidth: true
            visible: valid
            spacing: 2
            RowLayout {
                Layout.fillWidth: true
                Text { text: "残差·排名交互（含因子相关性）"; font.pixelSize: 9; color: "#64748B" }
                Item { Layout.fillWidth: true }
                Text { text: fp(report.residualRankingInteraction, 2); font.pixelSize: 9; color: "#94A3B8" }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "残差·交易成本（逐期扣费拖累 Σ(costAdj−raw)）"; font.pixelSize: 9; color: "#64748B" }
                Item { Layout.fillWidth: true }
                Text { text: fp(report.residualCosts, 2); font.pixelSize: 9; color: "#94A3B8" }
            }
        }

        // 累计贡献堆叠面积图
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 260
            visible: valid && curveLength > 0
            spacing: 2
            Text { text: "累计贡献堆叠（X=调仓期序号）"; font.pixelSize: 9; color: "#94A3B8" }
            ChartView {
                id: stackChart
                Layout.fillWidth: true
                Layout.fillHeight: true
                antialiasing: true
                legend.visible: true
                legend.font.pixelSize: 8
                legend.labelColor: "#94A3B8"
                backgroundColor: "transparent"
                plotAreaColor: "transparent"
                ValueAxis { id: stackX; min: 0; max: 1; labelsColor: "#64748B"; gridLineColor: "#1E293B"; labelFormat: "%.0f" }
                ValueAxis { id: stackY; labelsColor: "#94A3B8"; gridLineColor: "#1E293B"; labelFormat: "%.2f" }
            }
        }

        // 空态 (理论不可见: 页面侧按 valid 控制整体显隐)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            visible: !valid
            color: "#0B1220"
            radius: 6
            Text { anchors.centerIn: parent; text: "暂无因子归因数据（仅组合因子模式产出）"; font.pixelSize: 10; color: "#64748B" }
        }
    }

    // 表行: 因子名 + 数值 + 内嵌贡献条形
    component factorRowDelegate: Rectangle {
        width: ListView.view.width
        height: 26
        color: index % 2 ? "transparent" : "#1E293B"
        Row {
            anchors.fill: parent
            Text {
                width: 130; height: parent.height
                text: rowName(modelData)
                font.pixelSize: 9; color: "#F1F5F9"
                elide: Text.ElideRight; leftPadding: 4; verticalAlignment: Text.AlignVCenter
            }
            Text { width: 52; height: parent.height; text: fp(modelData.weight, 1); font.pixelSize: 9; color: "#94A3B8"; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
            Text { width: 48; height: parent.height; text: Number(modelData.rankIcMean).toFixed(3); font.pixelSize: 9; color: pnlColor(modelData.rankIcMean); horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
            Text { width: 48; height: parent.height; text: Number(modelData.rankIcir).toFixed(2); font.pixelSize: 9; color: pnlColor(modelData.rankIcir); horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
            Text { width: 86; height: parent.height; text: fp(modelData.longShortReturn, 2); font.pixelSize: 9; color: pnlColor(modelData.longShortReturn); horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
            Item {
                width: 150; height: parent.height
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: maxAbsContrib() > 0 ? Math.abs(Number(modelData.contribution) || 0) / maxAbsContrib() * 60 : 0
                    height: 7; radius: 3
                    color: pnlColor(modelData.contribution)
                    x: Number(modelData.contribution) >= 0 ? (parent.width - 70) / 2 : (parent.width - 70) / 2 - width
                }
                Text {
                    anchors.centerIn: parent
                    text: fp(modelData.contribution, 2)
                    font.pixelSize: 9
                    color: pnlColor(modelData.contribution)
                }
            }
            Text { width: 56; height: parent.height; text: modelData.coveredDays || 0; font.pixelSize: 9; color: "#94A3B8"; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
        }
    }
}
