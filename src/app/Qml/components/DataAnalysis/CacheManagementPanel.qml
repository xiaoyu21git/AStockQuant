import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property var cacheEntriesModel: null
    property var cachePreviewModel: null
    property var controller: null
    property int selectedIndex: -1

    color: "#0f172a"
    radius: 12
    border.width: 1
    border.color: "#1f2937"

    implicitHeight: contentColumn.implicitHeight + 24

    function modelCount(model) {
        if (!model) {
            return 0
        }
        if (model.count !== undefined) {
            return Number(model.count)
        }
        if (model.length !== undefined) {
            return Number(model.length)
        }
        return 0
    }

    function modelItemAt(model, index) {
        if (!model || index < 0) {
            return null
        }
        if (model.get) {
            return model.get(index)
        }
        if (model[index] !== undefined) {
            return model[index]
        }
        return null
    }

    function selectedEntry() {
        return modelItemAt(cacheEntriesModel, selectedIndex)
    }

    function selectedTitle() {
        var entry = selectedEntry()
        if (!entry) {
            return ""
        }
        return String(entry.displayName || entry.cacheKey || entry.description || "")
    }

    function selectedDescription() {
        var entry = selectedEntry()
        if (!entry) {
            return "请选择一个缓存项"
        }

        var parts = []
        if (entry.type) {
            parts.push(String(entry.type))
        }
        if (entry.sourceType) {
            parts.push("来源: " + String(entry.sourceType))
        }
        if (entry.rowCount !== undefined && entry.rowCount !== null) {
            parts.push("记录数: " + String(entry.rowCount))
        }
        if (entry.startDate || entry.endDate) {
            parts.push("区间: " + String(entry.startDate || "—") + " ~ " + String(entry.endDate || "—"))
        }
        if (entry.description) {
            parts.push(String(entry.description))
        }
        if (entry.cacheKey) {
            parts.push("Key: " + String(entry.cacheKey))
        }
        return parts.join(" · ")
    }

    function previewSelected() {
        if (!controller || selectedIndex < 0) {
            return
        }
        controller.previewCacheByIndex(selectedIndex)
    }

    function deleteSelected() {
        if (!controller || selectedIndex < 0) {
            return
        }

        if (controller.deleteCacheByIndex(selectedIndex)) {
            selectedIndex = -1
        }
    }

    function syncSelection() {
        var count = modelCount(cacheEntriesModel)
        if (count <= 0) {
            if (selectedIndex !== -1) {
                selectedIndex = -1
            }
            return
        }

        if (selectedIndex < 0 || selectedIndex >= count) {
            selectedIndex = 0
        }
    }

    onCacheEntriesModelChanged: syncSelection()

    Connections {
        target: cacheEntriesModel

        function onCountChanged() {
            syncSelection()
        }

        function onModelReset() {
            syncSelection()
        }
    }

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        anchors.margins: 14
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: "🗂 缓存管理"
                    font.pixelSize: 18
                    font.bold: true
                    color: "white"
                }

                Text {
                    text: "独立预览缓存内容，支持删除，不提供编辑入口；点击列表仅选中，预览需手动触发"
                    font.pixelSize: 11
                    color: "#94a3b8"
                }
            }

            Text {
                text: modelCount(cacheEntriesModel) + " 项"
                font.pixelSize: 12
                font.bold: true
                color: "#38bdf8"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            radius: 10
            color: "#111827"
            border.width: 1
            border.color: "#243244"
            implicitHeight: 172

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "缓存列表"
                        font.pixelSize: 13
                        font.bold: true
                        color: "#e2e8f0"
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: "预览选中"
                        enabled: controller && selectedIndex >= 0
                        onClicked: previewSelected()
                    }

                    Button {
                        text: "删除选中"
                        enabled: controller && selectedIndex >= 0
                        background: Rectangle {
                            radius: 6
                            color: parent.enabled ? "#dc2626" : "#374151"
                        }
                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            font.pixelSize: 12
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: deleteSelected()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 8
                    color: "#0b1220"
                    border.width: 1
                    border.color: "#243244"

                    ListView {
                        id: cacheListView
                        anchors.fill: parent
                        anchors.margins: 6
                        clip: true
                        model: cacheEntriesModel

                        delegate: Rectangle {
                            width: cacheListView.width
                            height: 54
                            radius: 6
                            color: selectedIndex === index ? "#1d4ed8" : (index % 2 === 0 ? "#0f172a" : "#111827")
                            border.width: 1
                            border.color: selectedIndex === index ? "#60a5fa" : "transparent"

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                Rectangle {
                                    width: 64
                                    height: 24
                                    radius: 12
                                    color: type === "dataset" ? "#0ea5e9" : "#22c55e"

                                    Text {
                                        anchors.centerIn: parent
                                        text: type === "dataset" ? "数据集" : "缓存"
                                        font.pixelSize: 10
                                        font.bold: true
                                        color: "white"
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        text: String(displayName || cacheKey || "未命名缓存")
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: "white"
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: selectedIndex === index ? selectedDescription() : String(description || cacheKey || "")
                                        font.pixelSize: 10
                                        color: "#94a3b8"
                                        elide: Text.ElideRight
                                    }
                                }

                                Text {
                                    text: rowCount !== undefined && rowCount !== null ? String(rowCount) + " 条" : ""
                                    font.pixelSize: 11
                                    color: "#93c5fd"
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    selectedIndex = index
                                }
                            }
                        }

                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            radius: 10
            color: "#111827"
            border.width: 1
            border.color: "#243244"
            implicitHeight: 420

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: selectedTitle() !== "" ? selectedTitle() : "缓存内容预览"
                            font.pixelSize: 15
                            font.bold: true
                            color: "white"
                        }

                        Text {
                            text: selectedDescription()
                            font.pixelSize: 11
                            color: "#94a3b8"
                            elide: Text.ElideRight
                        }
                    }

                    Text {
                        text: cachePreviewModel ? cachePreviewModel.pageSummary : ""
                        font.pixelSize: 11
                        color: "#38bdf8"
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 8
                    color: "#0b1220"
                    border.width: 1
                    border.color: "#243244"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        Row {
                            width: parent.width
                            height: 34
                            spacing: 0

                            Repeater {
                                model: [
                                    { label: "代码", width: 100 },
                                    { label: "名称", width: 120 },
                                    { label: "日期", width: 110 },
                                    { label: "来源", width: 110 },
                                    { label: "类型", width: 120 },
                                    { label: "记录数", width: 80 }
                                ]

                                delegate: Rectangle {
                                    width: modelData.width
                                    height: parent.height
                                    color: "#111827"
                                    border.width: 1
                                    border.color: "#243244"

                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        font.pixelSize: 11
                                        font.bold: true
                                        color: "#e2e8f0"
                                    }
                                }
                            }

                            Item { width: Math.max(0, parent.width - 640); height: 1 }
                        }

                        ListView {
                            id: previewListView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: cachePreviewModel

                            delegate: Rectangle {
                                width: previewListView.width
                                height: 36
                                color: index % 2 === 0 ? "#0f172a" : "#111827"
                                border.width: 1
                                border.color: "transparent"

                                Row {
                                    anchors.fill: parent
                                    Repeater {
                                        model: [
                                            { key: "code", width: 100 },
                                            { key: "name", width: 120 },
                                            { key: "date", width: 110 },
                                            { key: "source", width: 110 },
                                            { key: "dataType", width: 120 },
                                            { key: "recordCount", width: 80 }
                                        ]

                                        delegate: Rectangle {
                                            width: modelData.width
                                            height: parent.height
                                            color: "transparent"

                                            Text {
                                                anchors.fill: parent
                                                anchors.leftMargin: 8
                                                anchors.rightMargin: 8
                                                horizontalAlignment: Text.AlignLeft
                                                verticalAlignment: Text.AlignVCenter
                                                elide: Text.ElideRight
                                                text: {
                                                    var value = modelData.key === "recordCount" ? recordCount : (modelData.key === "code" ? code : (modelData.key === "name" ? name : (modelData.key === "date" ? date : (modelData.key === "source" ? source : dataType))))
                                                    return value === undefined || value === null || value === "" ? "—" : String(value)
                                                }
                                                font.pixelSize: 11
                                                color: "#e5e7eb"
                                            }
                                        }
                                    }

                                    Item { width: Math.max(0, parent.width - 640); height: 1 }
                                }
                            }

                            ScrollBar.vertical: ScrollBar {
                                policy: ScrollBar.AsNeeded
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        text: "上一页"
                        enabled: cachePreviewModel && cachePreviewModel.hasPreviousPage
                        onClicked: cachePreviewModel.previousPage()
                    }

                    Button {
                        text: "下一页"
                        enabled: cachePreviewModel && cachePreviewModel.hasNextPage
                        onClicked: cachePreviewModel.nextPage()
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: cachePreviewModel ? ("当前页 " + cachePreviewModel.currentPage + " / " + cachePreviewModel.totalPages) : ""
                        font.pixelSize: 11
                        color: "#94a3b8"
                    }
                }
            }
        }
    }
}
