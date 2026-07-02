import QtQuick 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0 as Bridge

Item {
    id: marketGrid

    property var marketData: []
    property var marketSections: []

    // ── 静态示例数据 ──
    readonly property var _sectors: [
        { name:"半导体",   chg:3.52, net:"+2.1亿", color:"#22c55e", sig:"🟢",
          leads: [{sym:"中芯国际",c:5.1},{sym:"北方华创",c:4.2},{sym:"韦尔股份",c:2.8}] },
        { name:"新能源车", chg:2.15, net:"+1.8亿", color:"#22c55e", sig:"🟢",
          leads: [{sym:"比亚迪",c:3.2},{sym:"宁德时代",c:2.1},{sym:"长城汽车",c:1.5}] },
        { name:"光伏",     chg:1.93, net:"+1.5亿", color:"#22c55e", sig:"🟢",
          leads: [{sym:"隆基绿能",c:2.8},{sym:"通威股份",c:1.9},{sym:"阳光电源",c:1.3}] },
        { name:"白酒",     chg:-0.35,net:"-0.8亿", color:"#ef4444", sig:"🔴",
          leads: [{sym:"贵州茅台",c:-0.5},{sym:"五粮液",c:-0.3},{sym:"泸州老窖",c:-0.2}] },
        { name:"军工",     chg:1.28, net:"+0.9亿", color:"#22c55e", sig:"🟢",
          leads: [{sym:"中航沈飞",c:1.9},{sym:"航发动力",c:1.2},{sym:"中航西飞",c:0.8}] },
        { name:"人工智能", chg:2.87, net:"+1.2亿", color:"#22c55e", sig:"🟢",
          leads: [{sym:"科大讯飞",c:4.3},{sym:"海康威视",c:2.1},{sym:"浪潮信息",c:1.9}] }
    ]
    property int selectedIdx: 0

    Rectangle {
        anchors.fill: parent; radius: 16; color: "#121828"
        border.color: "#2d3748"; border.width: 1; clip: true

        ColumnLayout {
            anchors.fill: parent; anchors.margins: 16; spacing: 8

            // ── 标题栏 ──
            Item { Layout.fillWidth: true; height: 28
                RowLayout { anchors.fill: parent
                    Text { text:"热门板块"; color:"#f1f5f9"; font.pixelSize:15; font.weight:Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    Text { text:"🔄"; font.pixelSize:13; color:"#3b82f6" }
                }
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
                                Text { text: modelData.sig; font.pixelSize: 11 }
                                Text { text: modelData.name; color: "#d0d0e0"; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text { text: (modelData.chg>0?"+":"")+modelData.chg.toFixed(2)+"%"
                                       color: modelData.color; font.pixelSize: 11; font.weight: Font.DemiBold }
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
                        Text { text: _sectors[selectedIdx].name + " 领涨股"; color: "#94a3b8"; font.pixelSize: 13; font.weight: Font.Medium }
                        Text { text: "主力 " + _sectors[selectedIdx].net; color: "#64748b"; font.pixelSize: 11 }

                        Repeater {
                            model: _sectors[selectedIdx].leads
                            Rectangle {
                                Layout.fillWidth: true; height: 38; radius: 6; color: "#121828"
                                RowLayout {
                                    anchors.fill: parent; anchors.margins: 10; spacing: 8
                                    Text { text: (index+1); color: "#666"; font.pixelSize: 11; font.weight: Font.Bold }
                                    Text { text: modelData.sym; color: "#f1f5f9"; font.pixelSize: 12; Layout.fillWidth: true }
                                    Text { text: (modelData.c>0?"+":"")+modelData.c.toFixed(1)+"%"
                                           color: modelData.c>=0?"#ef4444":"#10b981"; font.pixelSize: 12; font.weight: Font.DemiBold }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
