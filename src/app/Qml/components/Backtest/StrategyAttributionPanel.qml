import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15
import AStock.Bridge 1.0

// ═══ 策略归因面板 ═══
// 数据源: backtestResult.attribution (StrategyBacktestBridge::strategyAttributionToMap)
// 三块: Brinson 择时选股分解 + 行业盈亏(金额/占比双列) + 个股 Top5 盈亏
// 口径: 行业/个股=已实现盈亏(元); Brinson=对齐日重构毛收益
Rectangle {
    id: root
    radius: 8
    color: "#1E293B"
    Layout.fillWidth: true
    Layout.preferredHeight: contentCol.implicitHeight + 20

    property var report: null                       // backtestResult.attribution
    property bool valid: report ? (report.isValid === true) : false
    property var brinson: valid && report.brinson ? report.brinson : null
    property var sectors: valid && report.sectors ? report.sectors : []
    property var stocks: valid && report.stocks ? report.stocks : []

    // 个股 Top5 盈利 / Top5 亏损 (C++ 侧已按盈亏降序, 亏损列反转为亏损最多在前)
    property var topStocks: stocks.slice(0, 5)
    property var bottomStocks: {
        var n = stocks.length
        if (n <= 5) return stocks.slice(0).reverse()
        return stocks.slice(n - 5).reverse()
    }

    // 行业贡献最大绝对值 (条形图缩放基准)
    property double maxAbsContrib: {
        var m = 0
        for (var i = 0; i < sectors.length; i++) {
            var c = Math.abs(sectors[i].returnContribution || 0)
            if (c > m) m = c
        }
        return m
    }

    // ── 格式化工具 ──
    function fp(v, d) { var n = Number(v); return isNaN(n) ? "--" : (n * 100).toFixed(d === undefined ? 2 : d) + "%" }
    function fm(v) {
        var n = Number(v)
        if (isNaN(n)) return "--"
        var a = Math.abs(n)
        if (a >= 1e8) return (n / 1e8).toFixed(2) + "亿"
        if (a >= 1e4) return (n / 1e4).toFixed(1) + "万"
        return n.toFixed(0)
    }
    function pnlColor(v) { var n = Number(v); return isNaN(n) ? "#94A3B8" : (n >= 0 ? "#EF4444" : "#10B981") }
    // 恒等式裁决 (直接属性链绑定, 避免函数调用内部读取不被依赖跟踪)
    property bool identityPass: brinson && brinson.identityError !== undefined
                                && Math.abs(brinson.identityError) < 1e-6

    onReportChanged: refreshCurves()

    // 累计效应三线 (X=对齐交易日序号, Y=累计效应)
    function refreshCurves() {
        aeS.clear(); seS.clear(); ieS.clear()
        if (!brinson) return
        var n = Math.min(brinson.cumulativeAe.length, brinson.cumulativeSe.length, brinson.cumulativeIe.length)
        for (var i = 0; i < n; i++) {
            aeS.append(i, brinson.cumulativeAe[i])
            seS.append(i, brinson.cumulativeSe[i])
            ieS.append(i, brinson.cumulativeIe[i])
        }
        curveX.max = Math.max(1, n - 1)
    }

    ColumnLayout {
        id: contentCol
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // 标题行: 名称 + 数据质量警示 + 恒等式裁决徽标
        RowLayout {
            Layout.fillWidth: true
            Text { text: "策略归因"; font.pixelSize: 13; font.weight: Font.Bold; color: "#F1F5F9" }
            Rectangle {
                visible: brinson && brinson.lowDataQuality
                width: 84; height: 18; radius: 4; color: "#78350F"
                Text { anchors.centerIn: parent; text: "数据质量：低"; font.pixelSize: 9; color: "#F59E0B" }
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: valid && brinson
                text: identityPass ? "恒等式 ✓ 误差" + (brinson.identityError === 0 ? "0" : brinson.identityError.toExponential(1))
                                   : "恒等式 ✗ 误差" + (brinson ? brinson.identityError.toExponential(1) : "--")
                font.pixelSize: 9
                color: identityPass ? "#10B981" : "#EF4444"
            }
        }

        // 口径/降级说明 (含"Brinson 分解覆盖 X 个对齐交易日/共 Y 日")
        Text {
            Layout.fillWidth: true
            visible: report && (report.notice || "")
            text: report ? (report.notice || "") : ""
            font.pixelSize: 9
            color: "#64748B"
            wrapMode: Text.WordWrap
        }

        // 空态: 无归因数据只显示标题 + 说明
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            visible: !valid
            color: "#0B1220"
            radius: 6
            Text { anchors.centerIn: parent; text: "暂无策略归因数据（回测未产生有效持仓快照或基准数据）"; font.pixelSize: 10; color: "#64748B" }
        }

        // Brinson 五卡片: 超额 | 配置 | 选股 | 交互 | 组合/基准
        GridLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            visible: valid && brinson
            columns: 5
            columnSpacing: 8
            BrinsonCard { Layout.fillWidth: true; label: "超额收益"; value: brinson ? fp(brinson.excessReturn) : "--"; accent: brinson ? pnlColor(brinson.excessReturn) : "#94A3B8" }
            BrinsonCard { Layout.fillWidth: true; label: "配置效应"; value: brinson ? fp(brinson.allocationEffect) : "--"; accent: brinson ? pnlColor(brinson.allocationEffect) : "#94A3B8" }
            BrinsonCard { Layout.fillWidth: true; label: "选股效应"; value: brinson ? fp(brinson.selectionEffect) : "--"; accent: brinson ? pnlColor(brinson.selectionEffect) : "#94A3B8" }
            BrinsonCard { Layout.fillWidth: true; label: "交互效应"; value: brinson ? fp(brinson.interactionEffect) : "--"; accent: brinson ? pnlColor(brinson.interactionEffect) : "#94A3B8" }
            Column {
                Layout.fillWidth: true
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: brinson ? fp(brinson.portfolioReturn) : "--"; font.pixelSize: 14; font.weight: Font.Bold; color: brinson ? pnlColor(brinson.portfolioReturn) : "#94A3B8" }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: brinson ? "基准 " + fp(brinson.benchmarkReturn) : "--"; font.pixelSize: 9; color: "#64748B" }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "组合/基准收益"; font.pixelSize: 8; color: "#64748B" }
            }
        }

        // 累计效应曲线 + 年度分解小表
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 170
            visible: valid && brinson
            spacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 2
                Text { text: "Brinson 累计效应（对齐交易日序列）"; font.pixelSize: 9; color: "#94A3B8" }
                ChartView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    antialiasing: true
                    legend.visible: true
                    legend.font.pixelSize: 8
                    legend.labelColor: "#94A3B8"
                    backgroundColor: "transparent"
                    plotAreaColor: "transparent"
                    ValueAxis { id: curveX; min: 0; max: 1; labelsColor: "#64748B"; gridLineColor: "#1E293B"; labelFormat: "%.0f" }
                    ValueAxis { id: curveY; labelsColor: "#94A3B8"; gridLineColor: "#1E293B"; labelFormat: "%.2f" }
                    LineSeries { id: aeS; name: "配置AE"; axisX: curveX; axisY: curveY; color: "#F59E0B"; width: 2 }
                    LineSeries { id: seS; name: "选股SE"; axisX: curveX; axisY: curveY; color: "#38BDF8"; width: 2 }
                    LineSeries { id: ieS; name: "交互IE"; axisX: curveX; axisY: curveY; color: "#8B5CF6"; width: 2 }
                }
            }
            // 年度分解小表 (跨年回测才显示; 固定总宽 252 = 列宽 240 + 滚动条预留 12,
            // header 与数据行同宽同列 → 垂直滚动条不挤压数据行)
            ColumnLayout {
                Layout.preferredWidth: 252
                Layout.fillHeight: true
                visible: brinson && brinson.yearly && brinson.yearly.length > 1
                spacing: 2
                Text { text: "年度分解"; font.pixelSize: 9; color: "#94A3B8" }
                Row {
                    width: 240
                    height: 18
                    Text { width: 44; height: parent.height; text: "年"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; verticalAlignment: Text.AlignVCenter }
                    Text { width: 49; height: parent.height; text: "配置"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
                    Text { width: 49; height: parent.height; text: "选股"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
                    Text { width: 49; height: parent.height; text: "交互"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
                    Text { width: 49; height: parent.height; text: "超额"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
                }
                ListView {
                    Layout.preferredWidth: 252
                    Layout.preferredHeight: 132
                    clip: true
                    model: brinson ? brinson.yearly : []
                    delegate: Row {
                        width: 240
                        height: 18
                        Text { width: 44; text: modelData.year; font.pixelSize: 9; color: "#F1F5F9" }
                        Text { width: 49; text: fp(modelData.ae, 1); font.pixelSize: 9; color: pnlColor(modelData.ae); horizontalAlignment: Text.AlignRight }
                        Text { width: 49; text: fp(modelData.se, 1); font.pixelSize: 9; color: pnlColor(modelData.se); horizontalAlignment: Text.AlignRight }
                        Text { width: 49; text: fp(modelData.ie, 1); font.pixelSize: 9; color: pnlColor(modelData.ie); horizontalAlignment: Text.AlignRight }
                        Text { width: 49; text: fp(modelData.excess, 1); font.pixelSize: 9; color: pnlColor(modelData.excess); horizontalAlignment: Text.AlignRight }
                    }
                }
            }
        }

        // 行业盈亏表 + 个股 Top5 盈亏
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 220
            visible: valid
            spacing: 8
            // 左: 行业盈亏 (金额+占比双列, 内嵌水平条形; 固定总宽 454 = 列宽 442 + 滚动条预留 12,
            // header 与数据行同宽同列 → 垂直滚动条不挤压数据行)
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 2
                Text { text: "行业盈亏（已实现，元）"; font.pixelSize: 9; color: "#94A3B8" }
                Row {
                    width: 442
                    height: 18
                    Text { width: 90; height: parent.height; text: "行业"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; leftPadding: 4; verticalAlignment: Text.AlignVCenter }
                    Text { width: 72; height: parent.height; text: "盈亏(元)"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
                    Text { width: 60; height: parent.height; text: "占比"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
                    Text { width: 52; height: parent.height; text: "收益率"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
                    Text { width: 32; height: parent.height; text: "笔数"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
                    Text { width: 32; height: parent.height; text: "标的"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
                    Text { width: 52; height: parent.height; text: "组权重"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
                    Text { width: 52; height: parent.height; text: "基权重"; font.pixelSize: 8; color: "#64748B"; font.weight: Font.Bold; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
                }
                ListView {
                    Layout.preferredWidth: 454
                    Layout.preferredHeight: 150
                    clip: true
                    model: sectors
                    delegate: Rectangle {
                        width: 442
                        height: 20
                        color: index % 2 ? "transparent" : "#0B1220"
                        Row {
                            anchors.fill: parent
                            Text { width: 90; text: sectorDisplay(modelData); font.pixelSize: 9; color: "#F1F5F9"; elide: Text.ElideRight; leftPadding: 4 }
                            Text { width: 72; text: fm(modelData.totalRealizedPnl); font.pixelSize: 9; color: pnlColor(modelData.totalRealizedPnl); horizontalAlignment: Text.AlignRight }
                            Item {
                                width: 60; height: parent.height
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: maxAbsContrib > 0 ? Math.abs(modelData.returnContribution || 0) / maxAbsContrib * 32 : 0
                                    height: 6; radius: 3
                                    color: pnlColor(modelData.returnContribution || 0)
                                }
                                Text { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: fp(modelData.returnContribution || 0, 1); font.pixelSize: 8; color: pnlColor(modelData.returnContribution || 0) }
                            }
                            Text { width: 52; text: fp(modelData.realizedReturn, 1); font.pixelSize: 9; color: pnlColor(modelData.realizedReturn); horizontalAlignment: Text.AlignRight }
                            Text { width: 32; text: modelData.tradeCount || 0; font.pixelSize: 9; color: "#94A3B8"; horizontalAlignment: Text.AlignRight }
                            Text { width: 32; text: modelData.stockCount || 0; font.pixelSize: 9; color: "#94A3B8"; horizontalAlignment: Text.AlignRight }
                            Text { width: 52; text: fp(modelData.averageWeight, 1); font.pixelSize: 9; color: "#94A3B8"; horizontalAlignment: Text.AlignRight }
                            Text { width: 52; text: fp(modelData.benchmarkWeight, 1); font.pixelSize: 9; color: "#64748B"; horizontalAlignment: Text.AlignRight }
                        }
                    }
                }
            }
            // 右: 个股 Top5 盈利 / Top5 亏损 (固定总宽 270 = 列宽 256 + 滚动条预留 12 + 边距 2)
            ColumnLayout {
                Layout.preferredWidth: 270
                Layout.fillHeight: true
                spacing: 4
                Text { text: "个股 Top5 盈利"; font.pixelSize: 9; color: "#EF4444" }
                ListView {
                    Layout.preferredWidth: 268
                    Layout.preferredHeight: 88
                    clip: true
                    model: topStocks
                    delegate: stockRow
                }
                Text { text: "个股 Top5 亏损"; font.pixelSize: 9; color: "#10B981" }
                ListView {
                    Layout.preferredWidth: 268
                    Layout.preferredHeight: 88
                    clip: true
                    model: bottomStocks
                    delegate: stockRow
                }
            }
        }
    }

    // 行业名显示: 行业名表空 → 行业码 + 灰字提示
    function sectorDisplay(row) {
        if (!row) return "--"
        if (row.sectorName) return row.sectorName
        return row.sectorCode + " (未导入行业名)"
    }

    component BrinsonCard: Rectangle {
        property string label: ""
        property string value: "--"
        property color accent: "#F1F5F9"
        Layout.fillWidth: true
        radius: 6
        color: "#0B1220"
        implicitHeight: 48
        Column { anchors.centerIn: parent; spacing: 1
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: value; font.pixelSize: 14; font.weight: Font.Bold; color: accent }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: label; font.pixelSize: 8; color: "#64748B" }
        }
    }

    component stockRow: Rectangle {
        width: 256
        height: 18
        color: index % 2 ? "transparent" : "#0B1220"
        Row {
            anchors.fill: parent
            Text { width: 92; text: StrategyBridge.stockDisplayName(modelData.symbol); font.pixelSize: 9; color: "#F1F5F9"; elide: Text.ElideRight; leftPadding: 4 }
            Text { width: 36; text: "买" + (modelData.buyCount || 0); font.pixelSize: 9; color: "#64748B" }
            Text { width: 36; text: "卖" + (modelData.sellCount || 0); font.pixelSize: 9; color: "#64748B" }
            Text { width: 36; text: "胜" + (modelData.winCount || 0); font.pixelSize: 9; color: "#94A3B8" }
            Text { width: 56; text: fm(modelData.realizedPnl); font.pixelSize: 9; color: pnlColor(modelData.realizedPnl); horizontalAlignment: Text.AlignRight }
        }
    }
}
