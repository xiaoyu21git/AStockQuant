import QtQuick 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge

Item {
    id: marketGrid

    property var marketData: []
    property var marketSections: []

    // ── 从 C++ Bridge 获取实时板块数据 ──
    function loadSectors() {
        var raw = Bridge.MarketDataBridge ? Bridge.MarketDataBridge.sectorHeatData : []
        if (raw.length === 0) return false
        _sectors = raw
        return true
    }
    property var _sectors: []
    property int selectedIdx: 0

    Component.onCompleted: {
        if (loadSectors()) return
        if (Bridge.MarketDataBridge && typeof Bridge.MarketDataBridge.fetchSectorHeat === "function")
            Bridge.MarketDataBridge.fetchSectorHeat()
    }
    Connections {
        target: Bridge.MarketDataBridge
        enabled: Bridge.MarketDataBridge !== null
        function onSectorHeatDataChanged() { loadSectors() }
    }

    Rectangle {
        anchors.fill: parent; radius: 16; color: "#121828"
        border.color: "#2d3748"; border.width: 1; clip: true

        ColumnLayout {
            anchors.fill: parent; anchors.margins: 16; spacing: 8

            // ── 标题栏 ──
            Item { Layout.fillWidth: true; height: 20
                Text { anchors.right: parent.right; text:"🔄"; font.pixelSize:12; color:"#3b82f6" }
            }

            // ── 主体: 左列表 + 右明细 ──
            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 10

                // 左: 板块列表
                Rectangle {
                    Layout.preferredWidth: parent.width * 0.35; Layout.fillHeight: true
                    radius: 10; color: "#1a2235"
                    ListView {
                        id: leftList; anchors.fill: parent; anchors.margins: 6
                        clip: true; model: _sectors
                        delegate: Rectangle {
                            width: leftList.width; height: 36; radius: 6
                            color: index === selectedIdx ? "#1e3a5f" : "transparent"
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 6; spacing: 4
                                Text { text: (modelData.signal===0)?"🟢":((modelData.signal===1)?"🔴":((modelData.signal===2)?"🟡":"⚪")); font.pixelSize: 11 }
                                Text { text: modelData.name; color: "#d0d0e0"; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text { text: (modelData.chg>0?"+":"")+(modelData.chg||0).toFixed(2)+"%"
                                       color: (modelData.chg||0)>=0?"#ef4444":"#10b981"; font.pixelSize: 11; font.weight: Font.DemiBold }
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
                        Text { text: (_sectors[selectedIdx]&&_sectors[selectedIdx].name||"") + " 领涨股"; color: "#94a3b8"; font.pixelSize: 13; font.weight: Font.Medium }
                        Text { text: "主力 " + ((_sectors[selectedIdx]&&_sectors[selectedIdx].netIn||0>=0)?"+":"") + (Math.abs(_sectors[selectedIdx]&&_sectors[selectedIdx].netIn||0)>=1e8?(Math.abs(_sectors[selectedIdx].netIn)/1e8).toFixed(1)+"亿":(Math.abs(_sectors[selectedIdx]&&_sectors[selectedIdx].netIn||0)>=1e4?(Math.abs(_sectors[selectedIdx].netIn)/1e4).toFixed(0)+"万":(_sectors[selectedIdx]&&_sectors[selectedIdx].netIn||0).toFixed(0))); color: "#64748b"; font.pixelSize: 11 }

                        Repeater {
                            model: _sectors[selectedIdx] ? (_sectors[selectedIdx].leads || []) : []
                            Rectangle {
                                Layout.fillWidth: true; height: 38; radius: 6; color: "#121828"
                                RowLayout {
                                    anchors.fill: parent; anchors.margins: 10; spacing: 8
                                    Text { text: (index+1); color: "#666"; font.pixelSize: 11; font.weight: Font.Bold }
                                    Text { text: modelData.sym; color: "#f1f5f9"; font.pixelSize: 12; Layout.fillWidth: true }
                                    Text { text: (modelData.c>0?"+":"")+(modelData.c||0).toFixed(1)+"%"
                                           color: (modelData.c||0)>=0?"#ef4444":"#10b981"; font.pixelSize: 12; font.weight: Font.DemiBold }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
