import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0
import AStock.Bridge 1.0
import "../../components/DataAnalysis" as DataAnalysisComponents

Item {
    id: root
    anchors.fill: parent

    property string statusMessage: "等待刷新缓存列表"

    function rebuildCacheManagementModel() {
        cacheManagementModel.clear()

        var representedCacheKeys = {}
        for (var dataSetIndex = 0; dataSetIndex < dataSetInfosModel.count; dataSetIndex++) {
            var info = dataSetInfosModel.get(dataSetIndex)
            cacheManagementModel.append({
                type: "dataset",
                dataId: info.id,
                displayName: info.displayName,
                description: info.description,
                sourceType: info.sourceType,
                createdTime: info.createdTime,
                rowCount: info.rowCount,
                stockCodes: info.stockCodes || [],
                startDate: info.startDate,
                endDate: info.endDate,
                tags: info.tags || [],
                cacheKey: info.description
            })

            if (info.displayName) {
                representedCacheKeys[String(info.displayName)] = true
            }
            if (String(info.description || "").indexOf("从缓存存储的数据: ") === 0) {
                representedCacheKeys[String(info.description).substring("从缓存存储的数据: ".length)] = true
            }
            if (String(info.description || "").indexOf("从通用缓存存储的数据: ") === 0) {
                representedCacheKeys[String(info.description).substring("从通用缓存存储的数据: ".length)] = true
            }
        }

        for (var cacheIndex = 0; cacheIndex < cacheDisplayModel.count; cacheIndex++) {
            var cacheEntry = cacheDisplayModel.get(cacheIndex)
            var cacheKey = String(cacheEntry.cacheKey || "")
            if (!cacheKey || representedCacheKeys[cacheKey]) {
                continue
            }

            cacheManagementModel.append({
                type: "cache",
                dataId: -1,
                displayName: cacheEntry.displayName,
                description: cacheKey,
                sourceType: cacheEntry.type,
                createdTime: "",
                rowCount: cacheEntry.rowCount !== undefined ? cacheEntry.rowCount : 0,
                stockCodes: [],
                startDate: "",
                endDate: "",
                tags: [],
                cacheKey: cacheKey
            })
        }
    }

    function refreshCacheState() {
        dataFetchController.refreshCacheKeys()
        dataFetchController.refreshDataSetInfos()
    }

    Rectangle {
        anchors.fill: parent
        color: "#0a0f1a"

        Flickable {
            id: flickable
            anchors.fill: parent
            anchors.margins: 20
            contentWidth: width
            contentHeight: contentColumn.implicitHeight
            clip: true

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            Column {
                id: contentColumn
                width: flickable.width
                spacing: 18

                Rectangle {
                    width: parent.width
                    radius: 12
                    color: "#111827"
                    border.width: 1
                    border.color: "#243244"
                    implicitHeight: headerColumn.implicitHeight + 28

                    ColumnLayout {
                        id: headerColumn
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4

                                Text {
                                    text: "缓存管理"
                                    font.pixelSize: 22
                                    font.bold: true
                                    color: "white"
                                }

                                Text {
                                    text: "独立查看缓存列表、预览缓存内容并删除缓存项，不提供编辑入口。"
                                    font.pixelSize: 13
                                    color: "#94a3b8"
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }

                            Button {
                                text: "刷新"
                                onClicked: refreshCacheState()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Rectangle {
                                Layout.preferredWidth: 180
                                Layout.fillHeight: true
                                radius: 10
                                color: "#0f172a"
                                border.width: 1
                                border.color: "#1d4ed8"
                                implicitHeight: 64

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 4

                                    Text {
                                        text: "缓存总项"
                                        font.pixelSize: 11
                                        color: "#93c5fd"
                                        horizontalAlignment: Text.AlignHCenter
                                        width: parent.width
                                    }

                                    Text {
                                        text: cacheManagementModel.count
                                        font.pixelSize: 22
                                        font.bold: true
                                        color: "white"
                                        horizontalAlignment: Text.AlignHCenter
                                        width: parent.width
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 10
                                color: "#0f172a"
                                border.width: 1
                                border.color: "#243244"
                                implicitHeight: 64

                                Text {
                                    id: statusText
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    text: root.statusMessage
                                    font.pixelSize: 12
                                    color: "#cbd5e1"
                                    wrapMode: Text.WordWrap
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }
                }

                DataAnalysisComponents.CacheManagementPanel {
                    width: parent.width
                    cacheEntriesModel: cacheManagementModel
                    cachePreviewModel: dataFetchController.cachePreviewModel
                    cacheDetailPreviewModel: dataFetchController.cacheDetailPreviewModel
                    controller: dataFetchController
                }
            }
        }
    }

    ListModel {
        id: cacheDisplayModel
    }

    ListModel {
        id: dataSetInfosModel
    }

    ListModel {
        id: cacheManagementModel
    }

    DataFetchController {
        id: dataFetchController

        onCacheKeysRefreshed: function(cacheKeys) {
            root.statusMessage = "缓存键列表已刷新"
            cacheDisplayModel.clear()
            for (var cacheKeyIndex = 0; cacheKeyIndex < cacheKeys.length; cacheKeyIndex++) {
                var currentCacheKey = cacheKeys[cacheKeyIndex]
                var cacheDisplayName = "📁 缓存: " + currentCacheKey

                if (currentCacheKey.startsWith("data:stock:ALL_")) {
                    cacheDisplayName = "📊 数据集: " + currentCacheKey.substring(15)
                } else if (currentCacheKey.startsWith("dataset_")) {
                    cacheDisplayName = "📊 数据集: " + currentCacheKey.substring(8).replace(/_/g, " 至 ")
                }

                cacheDisplayModel.append({
                    displayName: cacheDisplayName,
                    index: cacheKeyIndex,
                    type: "cache",
                    cacheKey: currentCacheKey
                })
            }
            rebuildCacheManagementModel()
        }

        onDataSetInfosRefreshed: function(dataSetInfos) {
            root.statusMessage = "数据集信息已刷新"
            dataSetInfosModel.clear()
            for (var dataSetInfoIndex = 0; dataSetInfoIndex < dataSetInfos.length; dataSetInfoIndex++) {
                var info = dataSetInfos[dataSetInfoIndex]
                dataSetInfosModel.append({
                    id: info.id,
                    displayName: info.displayName,
                    description: info.description,
                    sourceType: info.sourceType,
                    createdTime: info.createdTime,
                    rowCount: info.rowCount,
                    stockCodes: info.stockCodes || [],
                    startDate: info.startDate,
                    endDate: info.endDate,
                    tags: info.tags || []
                })
            }
            rebuildCacheManagementModel()
        }

        onStatusMessageChanged: {
            if (statusMessage && statusMessage !== "") {
                root.statusMessage = statusMessage
            }
        }
    }

    Component.onCompleted: refreshCacheState()

    onVisibleChanged: {
        if (visible) {
            refreshCacheState()
        }
    }
}