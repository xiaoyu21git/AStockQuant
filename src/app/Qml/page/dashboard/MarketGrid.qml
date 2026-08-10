import QtQuick 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge

Item {
    id: marketGrid

    property var marketData: []
    property var marketSections: []

    // ── 板块数据 & 排序 ──
    function loadSectors() {
        var raw = Bridge.MarketDataBridge ? Bridge.MarketDataBridge.sectorHeatData : []
        if (raw.length === 0) return false
        _sectors = raw
        return true
    }
    property var _sectors: []
    property int selectedIdx: 0
    property int sortMode: 0  // 0=综合 1=涨幅 2=资金

    function applySort(list) {
        var copy = list.slice()
        if (sortMode === 1) {
            copy.sort(function(a, b) { return (b.chg || 0) - (a.chg || 0) })
        } else if (sortMode === 2) {
            copy.sort(function(a, b) { return (b.netIn || 0) - (a.netIn || 0) })
        }
        return copy
    }
    readonly property var sortedSectors: applySort(_sectors)

    function onSortChanged(mode) {
        sortMode = mode
        selectedIdx = 0
    }

    function formatNetIn(v) {
        var n = v || 0
        var abs = Math.abs(n)
        var sign = n >= 0 ? "+" : ""
        if (abs >= 1e8) return sign + (abs / 1e8).toFixed(1) + "亿"
        if (abs >= 1e4) return sign + (abs / 1e4).toFixed(0) + "万"
        return sign + abs.toFixed(0)
    }

    function formatTrend(trend) {
        var t = trend || 0
        if (t > 1) return "↑" + t
        if (t < -1) return "↓" + Math.abs(t)
        if (t === 1) return "↑"
        if (t === -1) return "↓"
        return "→"
    }

    function trendColor(trend) {
        var t = trend || 0
        if (t > 0) return "#ef4444"
        if (t < 0) return "#10b981"
        return "#64748b"
    }

    function countUpDown(leads) {
        var up = 0
        var arr = leads || []
        var cnt = Math.min(arr.length, 5)
        for (var i = 0; i < cnt; i++) {
            if ((arr[i].c || 0) > 0) up++
        }
        return "↑" + up + "↓" + (cnt - up)
    }

    // ── 初始加载: 同步拉取 → 立即重读, 不依赖异步信号 ──
    Component.onCompleted: {
        if (loadSectors()) return
        if (Bridge.MarketDataBridge && typeof Bridge.MarketDataBridge.fetchSectorHeat === "function") {
            Bridge.MarketDataBridge.fetchSectorHeat()
            loadSectors()
        }
    }
    // ── 后台线程推送: 收到信号后重读数据 ──
    Connections {
        target: Bridge.MarketDataBridge
        enabled: Bridge.MarketDataBridge !== null
        function onSectorHeatDataChanged() { loadSectors() }
    }
    // ── 页面切回: 同步刷新 ──
    onVisibleChanged: {
        if (visible) {
            if (Bridge.MarketDataBridge && typeof Bridge.MarketDataBridge.fetchSectorHeat === "function") {
                Bridge.MarketDataBridge.fetchSectorHeat()
                loadSectors()
            }
        }
    }

    Rectangle {
        anchors.fill: parent; radius: 16; color: "#121828"
        border.color: "#2d3748"; border.width: 1; clip: true

        ColumnLayout {
            anchors.fill: parent; anchors.margins: 16; spacing: 8

            // ── 标题栏: 排序切换 ──
            Item { Layout.fillWidth: true; height: 24
                Row { spacing: 2
                    Repeater {
                        model: ["涨幅", "资金", "综合"]
                        Rectangle {
                            width: 44; height: 22; radius: 4
                            color: sortMode === index ? "#1e3a5f" : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: modelData; font.pixelSize: 11
                                color: sortMode === index ? "#60a5fa" : "#64748b"
                            }
                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: onSortChanged(index)
                            }
                        }
                    }
                }
                Text {
                    anchors.right: parent.right
                    text: "🔄"; font.pixelSize: 12; color: "#3b82f6"
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (Bridge.MarketDataBridge && typeof Bridge.MarketDataBridge.fetchSectorHeat === "function")
                                Bridge.MarketDataBridge.fetchSectorHeat()
                        }
                    }
                }
            }

            // ── 主体: 左列表 + 右明细 ──
            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 10

                // 左: 板块列表
                Rectangle {
                    Layout.preferredWidth: parent.width * 0.38; Layout.fillHeight: true
                    radius: 10; color: "#1a2235"
                    ListView {
                        id: leftList; anchors.fill: parent; anchors.margins: 6
                        clip: true; model: sortedSectors
                        delegate: Rectangle {
                            width: leftList.width; height: 44; radius: 6
                            color: index === selectedIdx ? "#1e3a5f" : "transparent"
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 5; spacing: 2
                                // 上行: 信号+名称+涨跌幅
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 4
                                    Text { text: (modelData.signal===0)?"🟢":((modelData.signal===1)?"🔴":((modelData.signal===2)?"🟡":"⚪")); font.pixelSize: 10 }
                                    Text { text: modelData.name||""; color: "#d0d0e0"; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Text {
                                        text: ((modelData.chg||0) > 0 ? "+" : "") + (modelData.chg||0).toFixed(2) + "%"
                                        color: (modelData.chg||0) >= 0 ? "#ef4444" : "#10b981"
                                        font.pixelSize: 11; font.weight: Font.DemiBold
                                    }
                                }
                                // 下行: 涨跌比 + 趋势 + 主力净流入
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 6
                                    Text { text: countUpDown(modelData.leads); color: "#64748b"; font.pixelSize: 10 }
                                    Text {
                                        text: formatTrend(modelData.netInTrend)
                                        color: trendColor(modelData.netInTrend)
                                        font.pixelSize: 10; font.weight: Font.DemiBold
                                    }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        text: formatNetIn(modelData.netIn)
                                        color: (modelData.netIn||0) >= 0 ? "#ef4444" : "#10b981"
                                        font.pixelSize: 10
                                    }
                                }
                            }
                            MouseArea { anchors.fill: parent; onClicked: selectedIdx = index }
                        }
                    }
                }

                // 右: 领涨股
                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: 10; color: "#1a2235"
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 10; spacing: 6
                        Text {
                            text: (sortedSectors[selectedIdx] && sortedSectors[selectedIdx].name || "") + " 领涨股"
                            color: "#94a3b8"; font.pixelSize: 13; font.weight: Font.Medium
                        }
                        Text {
                            text: "主力 " + formatNetIn(sortedSectors[selectedIdx] && sortedSectors[selectedIdx].netIn || 0)
                              + "  " + formatTrend(sortedSectors[selectedIdx] && sortedSectors[selectedIdx].netInTrend || 0)
                            color: "#64748b"; font.pixelSize: 11
                        }

                        Repeater {
                            model: sortedSectors[selectedIdx] ? (sortedSectors[selectedIdx].leads || []) : []
                            Rectangle {
                                Layout.fillWidth: true; height: 48; radius: 6; color: "#121828"
                                RowLayout {
                                    anchors.fill: parent; anchors.margins: 10; spacing: 8
                                    Text { text: (index+1); color: "#666"; font.pixelSize: 11; font.weight: Font.Bold }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 0
                                        Text { text: modelData.sym||""; color: "#f1f5f9"; font.pixelSize: 12 }
                                        Text { text: modelData.name||""; color: "#94a3b8"; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                                    }
                                    Text {
                                        text: (modelData.c > 0 ? "+" : "") + (modelData.c||0).toFixed(1) + "%"
                                        color: (modelData.c||0) >= 0 ? "#ef4444" : "#10b981"
                                        font.pixelSize: 12; font.weight: Font.DemiBold
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
