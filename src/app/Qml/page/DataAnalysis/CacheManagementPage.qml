import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AStock.Bridge 1.0

Item {
    id: root
    anchors.fill: parent

    ListModel { id: cacheModel }

    DataFetchController {
        id: dataFetchController
        onDataSetInfosRefreshed: function(infos) {
            cacheModel.clear()
            for (var i = 0; i < infos.length; i++) {
                var info = infos[i]
                cacheModel.append({
                    id: info.id,
                    displayName: info.displayName || ("#" + info.id),
                    sourceType: info.sourceType || "",
                    rowCount: info.rowCount || 0,
                    startDate: info.startDate || "",
                    endDate: info.endDate || "",
                    createdTime: info.createdTime || ""
                })
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 12

        RowLayout {
            Text { text: "缓存管理"; font.pixelSize: 18; font.bold: true; color: "white"; Layout.fillWidth: true }
            Button {
                text: "刷新"
                onClicked: dataFetchController.refreshDataSetInfos()
                background: Rectangle { color: "#1e3a5f"; radius: 4 }
                contentItem: Text { text: "刷新"; color: "white"; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#4b5563" }

        Text { text: "共 " + cacheModel.count + " 条"; color: "#94a3b8"; font.pixelSize: 12 }

        ListView {
            id: listView
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; model: cacheModel; spacing: 4

            header: Rectangle {
                width: listView.width; height: 30; color: "#1e293b"
                RowLayout {
                    anchors.fill: parent; anchors.margins: 4
                    Text { text: "ID"; Layout.preferredWidth: 60; color: "#94a3b8"; font.pixelSize: 11; font.bold: true }
                    Text { text: "名称"; Layout.fillWidth: true; color: "#94a3b8"; font.pixelSize: 11; font.bold: true }
                    Text { text: "行数"; Layout.preferredWidth: 80; color: "#94a3b8"; font.pixelSize: 11; font.bold: true }
                    Text { text: "操作"; Layout.preferredWidth: 80; color: "#94a3b8"; font.pixelSize: 11; font.bold: true }
                }
            }

            delegate: Rectangle {
                width: listView.width; height: 40; color: index % 2 === 0 ? "#1a2332" : "#162230"
                RowLayout {
                    anchors.fill: parent; anchors.margins: 4
                    Text { text: "#" + model.id; Layout.preferredWidth: 60; color: "#e2e8f0"; font.pixelSize: 12 }
                    Text { text: model.displayName || ""; Layout.fillWidth: true; color: "#cbd5e1"; font.pixelSize: 12; elide: Text.ElideRight }
                    Text { text: model.rowCount.toLocaleString(); Layout.preferredWidth: 80; color: "#94a3b8"; font.pixelSize: 11 }
                    Button {
                        Layout.preferredWidth: 70; Layout.preferredHeight: 28
                        text: "删除"
                        onClicked: { if (model.id > 0) dataFetchController.removeDataSet(model.id) }
                        background: Rectangle { color: "#991b1b"; radius: 4 }
                        contentItem: Text { text: "删除"; color: "white"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }
            }
        }
    }

    Component.onCompleted: dataFetchController.refreshDataSetInfos()
}
