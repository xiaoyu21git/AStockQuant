import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0

Item {
    id: root
    property string selectedStrategyId: ""
    property string selectedStrategyName: ""
    property int selectedRow: -1

    signal resultSelected(var result)
    function refreshPerformance() { perfModel.refresh() }

    StrategyPerformanceModel {
        id: perfModel
        strategyId: root.selectedStrategyId
        onErrorOccurred: function(msg) { console.warn("绩效查询:", msg) }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            Text { text: "策略绩效"; font.pixelSize: 20; font.weight: Font.Black; color: "#F1F5F9" }
            Item { Layout.fillWidth: true }
            Rectangle { width: 80; height: 32; radius: 6; color: "#3B82F6"
                Text { anchors.centerIn: parent; text: "刷新"; font.pixelSize: 12; color: "white" }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: perfModel.refresh() } }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: 10; color: "#1E293B"; clip: true
            ListView {
                id: historyList
                anchors.fill: parent; anchors.margins: 4
                model: perfModel; spacing: 2
                headerPositioning: ListView.OverlayHeader
                header: Rectangle {
                    width: historyList.width; height: 28; z: 2; color: "#1E293B"
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        Text { width: 150; text: "回测时间"; font.pixelSize: 11; color: "#94A3B8"; font.weight: Font.DemiBold }
                        Text { width: 80; text: "总收益";   font.pixelSize: 11; color: "#94A3B8"; font.weight: Font.DemiBold }
                        Text { width: 80; text: "年化";     font.pixelSize: 11; color: "#94A3B8"; font.weight: Font.DemiBold }
                        Text { width: 70; text: "夏普";     font.pixelSize: 11; color: "#94A3B8"; font.weight: Font.DemiBold }
                        Text { width: 80; text: "最大回撤"; font.pixelSize: 11; color: "#94A3B8"; font.weight: Font.DemiBold }
                        Text { width: 70; text: "胜率";     font.pixelSize: 11; color: "#94A3B8"; font.weight: Font.DemiBold }
                        Text { width: 200; text: "区间";     font.pixelSize: 11; color: "#94A3B8"; font.weight: Font.DemiBold }
                    }
                }
                delegate: Rectangle {
                    width: historyList.width; height: 34; radius: 4
                    color: root.selectedRow === index ? "#1E3A5F" : "transparent"
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: { root.selectedRow = index; root.resultSelected(perfModel.loadResultDetail(index)) }
                    }
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        Text { width: 150; text: runAt || "--"; font.pixelSize: 12; color: "#CBD5E1" }
                        Text { width: 80; text: (totalReturn*100).toFixed(2)+"%"; font.pixelSize: 12; color: totalReturn>=0?"#EF4444":"#22C55E" }
                        Text { width: 80; text: (annualizedReturn*100).toFixed(2)+"%"; font.pixelSize: 12; color: annualizedReturn>=0?"#EF4444":"#22C55E" }
                        Text { width: 70; text: sharpeRatio.toFixed(3); font.pixelSize: 12; color: "#F1F5F9" }
                        Text { width: 80; text: (maxDrawdown*100).toFixed(1)+"%"; font.pixelSize: 12; color: "#F59E0B" }
                        Text { width: 70; text: (winRate*100).toFixed(1)+"%"; font.pixelSize: 12; color: "#F1F5F9" }
                        Text { width: 200; text: (startDate||"--")+" ~ "+(endDate||"--"); font.pixelSize: 11; color: "#94A3B8" }
                    }
                }
            }
        }
    }
}