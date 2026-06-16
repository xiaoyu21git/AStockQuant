import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0

Item {
    id: root

    property string selectedStrategyId: ""
    property string selectedStrategyName: ""
    property var selectedResult: null
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
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Text {
                text: "策略绩效 · " + (selectedStrategyName || selectedStrategyId || "")
                font.pixelSize: 20; font.weight: Font.Black; color: "#F1F5F9"
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                width: 100; height: 36; radius: 8; color: "#3B82F6"
                Text { anchors.centerIn: parent; text: "刷新"; font.pixelSize: 13; color: "white" }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: perfModel.refresh() }
            }
        }

        // 历史列表 — 使用 QAbstractListModel 角色绑定
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 220
            radius: 12; color: "#1E293B"; border.width: 1; border.color: "#334155"
            clip: true

            ListView {
                id: historyList
                anchors.fill: parent; anchors.margins: 8
                model: perfModel
                spacing: 4

                header: Row {
                    width: historyList.width
                    Text { width: 140; text: "回测时间"; font.size: 11; color: "#94A3B8"; font.weight: Font.DemiBold }
                    Text { width: 70; text: "总收益";  font.size: 11; color: "#94A3B8"; font.weight: Font.DemiBold }
                    Text { width: 70; text: "年化";    font.size: 11; color: "#94A3B8"; font.weight: Font.DemiBold }
                    Text { width: 70; text: "夏普";    font.size: 11; color: "#94A3B8"; font.weight: Font.DemiBold }
                    Text { width: 70; text: "回撤";    font.size: 11; color: "#94A3B8"; font.weight: Font.DemiBold }
                    Text { width: 60; text: "胜率";    font.size: 11; color: "#94A3B8"; font.weight: Font.DemiBold }
                    Text { width: 50; text: "类型";    font.size: 11; color: "#94A3B8"; font.weight: Font.DemiBold }
                }

                delegate: Rectangle {
                    width: historyList.width; height: 32; radius: 6
                    color: root.selectedRow === index ? "#1E3A5F" : "transparent"
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.selectedRow = index
                            root.selectedResult = perfModel.loadResultDetail(index)
                            root.resultSelected(root.selectedResult)
                        }
                    }
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        Text { width: 140; text: runAt || "--"; font.size: 12; color: "#CBD5E1" }
                        Text { width: 70; text: (totalReturn * 100).toFixed(2) + "%"; font.size: 12
                            color: totalReturn >= 0 ? "#EF4444" : "#22C55E" }
                        Text { width: 70; text: (annualizedReturn * 100).toFixed(2) + "%"; font.size: 12
                            color: annualizedReturn >= 0 ? "#EF4444" : "#22C55E" }
                        Text { width: 70; text: sharpeRatio.toFixed(3); font.size: 12; color: "#F1F5F9" }
                        Text { width: 70; text: (maxDrawdown * 100).toFixed(1) + "%"; font.size: 12; color: "#22C55E" }
                        Text { width: 60; text: (winRate * 100).toFixed(1) + "%"; font.size: 12; color: "#F1F5F9" }
                        Text { width: 50; text: behaviorLabel(behaviorKind); font.size: 11; color: "#94A3B8" }
                    }
                }
            }
        }

        // 详情卡片
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 140
            radius: 12; color: "#1E293B"; border.width: 1; border.color: "#334155"
            visible: selectedResult !== null

            RowLayout {
                anchors.fill: parent; anchors.margins: 16; spacing: 20
                detailCard("夏普",       (selectedResult ? perfModel.data(perfModel.index(selectedRow,0), StrategyPerformanceModel.SharpeRatioRole) : 0).toFixed(3), true)
                detailCard("年化收益",   ((selectedResult ? perfModel.data(perfModel.index(selectedRow,0), StrategyPerformanceModel.AnnualizedReturnRole) : 0) * 100).toFixed(2) + "%", false)
                detailCard("最大回撤",   ((selectedResult ? perfModel.data(perfModel.index(selectedRow,0), StrategyPerformanceModel.MaxDrawdownRole) : 0) * 100).toFixed(1) + "%", false)
                detailCard("胜率",       ((selectedResult ? perfModel.data(perfModel.index(selectedRow,0), StrategyPerformanceModel.WinRateRole) : 0) * 100).toFixed(1) + "%", false)
                detailCard("Sortino",    (selectedResult ? perfModel.data(perfModel.index(selectedRow,0), StrategyPerformanceModel.SortinoRatioRole) : 0).toFixed(3), false)
                detailCard("Calmar",     (selectedResult ? perfModel.data(perfModel.index(selectedRow,0), StrategyPerformanceModel.CalmarRatioRole) : 0).toFixed(3), false)
                detailCard("利润因子",   (selectedResult ? perfModel.data(perfModel.index(selectedRow,0), StrategyPerformanceModel.ProfitFactorRole) : 0).toFixed(2), false)
            }
        }

        Item { Layout.fillHeight: true }
    }

    function behaviorLabel(kind) {
        switch (Number(kind)) { case 0: return "趋势"; case 1: return "回归"; case 2: return "动量";
        case 3: return "套利"; case 4: return "因子"; case 5: return "ML"; case 6: return "事件";
        case 7: return "高频"; case 8: return "自定义"; default: return "?" }
    }

    function detailCard(lbl, val, emph) {
        var c = detailCardComp.createObject(null, { cardLabel: lbl, cardValue: val, cardEmphasize: emph })
        return c
    }

    Component {
        id: detailCardComp
        Rectangle {
            property string cardLabel: ""
            property string cardValue: ""
            property bool cardEmphasize: false
            width: 120; height: 90; radius: 10
            color: cardEmphasize ? "#172235" : "#121A2B"
            border.width: 1; border.color: cardEmphasize ? "#FB923C" : "#243247"
            Column {
                anchors.centerIn: parent; spacing: 6
                Text { anchors.horizontalCenter: parent.horizontalCenter
                    text: cardValue; font.pixelSize: 22; font.weight: Font.Black
                    color: cardEmphasize ? "#F97316" : "#F1F5F9" }
                Text { anchors.horizontalCenter: parent.horizontalCenter
                    text: cardLabel; font.pixelSize: 11; color: "#94A3B8" }
            }
        }
    }
}
