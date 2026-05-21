import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property var cacheEntriesModel: null
    property var cachePreviewModel: null
    property var cacheDetailPreviewModel: null
    property var controller: null
    property int selectedIndex: -1
    property string pendingExportFormat: "json"

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
        symbolFilterField.text = ""
        startDateFilterField.text = ""
        endDateFilterField.text = ""
        controller.previewCacheByIndex(selectedIndex)
    }

    function previewFields() {
        return cacheDetailPreviewModel ? cacheDetailPreviewModel.visibleFields : []
    }

    function previewRows() {
        return cacheDetailPreviewModel ? cacheDetailPreviewModel.visibleRows : []
    }

    function previewDates() {
        return cacheDetailPreviewModel ? cacheDetailPreviewModel.availableDates : []
    }

    function previewFieldGroup() {
        return cacheDetailPreviewModel ? String(cacheDetailPreviewModel.fieldGroup || "daily") : "daily"
    }

    function selectedPreviewDate() {
        return cacheDetailPreviewModel ? String(cacheDetailPreviewModel.selectedDate || "") : ""
    }

    function applyPreviewFilters(groupKey) {
        if (!cacheDetailPreviewModel) {
            return
        }
        var nextGroup = groupKey !== undefined ? String(groupKey || "") : previewFieldGroup()
        cacheDetailPreviewModel.applyFilters(
            symbolFilterField.text,
            startDateFilterField.text,
            endDateFilterField.text,
            nextGroup
        )
    }

    function clearPreviewFilters() {
        symbolFilterField.text = ""
        startDateFilterField.text = ""
        endDateFilterField.text = ""
        if (cacheDetailPreviewModel) {
            cacheDetailPreviewModel.clearFilters()
        }
    }

    function extractDateToken(value) {
        var text = String(value === undefined || value === null ? "" : value).trim()
        if (text.length >= 10 && /^\d{4}-\d{2}-\d{2}/.test(text)) {
            return text.substring(0, 10)
        }
        if (/^\d{8}$/.test(text)) {
            return text.substring(0, 4) + "-" + text.substring(4, 6) + "-" + text.substring(6, 8)
        }
        return ""
    }

    function openExportDialog(format) {
        if (!controller || !cacheDetailPreviewModel || cacheDetailPreviewModel.totalCount <= 0) {
            return
        }
        pendingExportFormat = format
        exportFileDialog.nameFilters = format === "csv"
            ? ["CSV 文件 (*.csv)"]
            : ["JSON 文件 (*.json)"]
        exportFileDialog.open()
    }

    function columnWidth(fieldName) {
        var field = String(fieldName || "").toLowerCase()
        if (field === "symbol" || field === "code" || field === "stock_code") {
            return 100
        }
        if (field === "name") {
            return 140
        }
        if (field.indexOf("date") >= 0 || field.indexOf("time") >= 0) {
            return 118
        }
        if (field === "report_type" || field === "industry_code") {
            return 110
        }
        return 108
    }

    function totalColumnWidth() {
        var fields = previewFields()
        var width = 0
        for (var i = 0; i < fields.length; ++i) {
            width += columnWidth(fields[i])
        }
        return Math.max(width, previewTableViewport.width)
    }

    function formatFieldValue(value) {
        if (value === undefined || value === null || value === "") {
            return "—"
        }
        if (typeof value === "number") {
            if (!isFinite(value)) {
                return "—"
            }
            if (Math.abs(value) >= 1000 || Math.floor(value) === value) {
                return String(value)
            }
            return value.toFixed(6).replace(/\.?0+$/, "")
        }
        return String(value)
    }

    function previewEmptyMessage() {
        if (!cacheDetailPreviewModel) {
            return "请选择缓存项后点击预览"
        }
        if (modelCount(previewFields()) === 0) {
            return "当前缓存没有可展示的清洗字段"
        }
        if (cacheDetailPreviewModel.totalCount <= 0) {
            return "筛选结果为空"
        }
        return ""
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
                        text: cacheDetailPreviewModel ? cacheDetailPreviewModel.pageSummary : ""
                        font.pixelSize: 11
                        color: "#38bdf8"
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: symbolFilterField
                        Layout.preferredWidth: 150
                        placeholderText: "股票代码"
                        color: "#e5e7eb"
                        selectByMouse: true
                        background: Rectangle {
                            radius: 6
                            color: "#0f172a"
                            border.width: 1
                            border.color: "#243244"
                        }
                    }

                    TextField {
                        id: startDateFilterField
                        Layout.preferredWidth: 130
                        placeholderText: "开始日期 YYYY-MM-DD"
                        color: "#e5e7eb"
                        selectByMouse: true
                        background: Rectangle {
                            radius: 6
                            color: "#0f172a"
                            border.width: 1
                            border.color: "#243244"
                        }
                    }

                    TextField {
                        id: endDateFilterField
                        Layout.preferredWidth: 130
                        placeholderText: "结束日期 YYYY-MM-DD"
                        color: "#e5e7eb"
                        selectByMouse: true
                        background: Rectangle {
                            radius: 6
                            color: "#0f172a"
                            border.width: 1
                            border.color: "#243244"
                        }
                    }

                    Button {
                        text: "应用筛选"
                        enabled: cacheDetailPreviewModel
                        onClicked: applyPreviewFilters()
                    }

                    Button {
                        text: "清空筛选"
                        enabled: cacheDetailPreviewModel
                        onClicked: clearPreviewFilters()
                    }

                    Button {
                        text: "导出 JSON"
                        enabled: controller && cacheDetailPreviewModel && cacheDetailPreviewModel.totalCount > 0
                        onClicked: openExportDialog("json")
                    }

                    Button {
                        text: "导出 CSV"
                        enabled: controller && cacheDetailPreviewModel && cacheDetailPreviewModel.totalCount > 0
                        onClicked: openExportDialog("csv")
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: cacheDetailPreviewModel
                            ? ("日线字段 " + modelCount(cacheDetailPreviewModel.dailyFields) + " · 财务字段 " + modelCount(cacheDetailPreviewModel.financialFields))
                            : ""
                        font.pixelSize: 11
                        color: "#94a3b8"
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Repeater {
                        model: [
                            { key: "daily", label: "日线字段", count: cacheDetailPreviewModel ? modelCount(cacheDetailPreviewModel.dailyFields) : 0 },
                            { key: "financial", label: "财务字段", count: cacheDetailPreviewModel ? modelCount(cacheDetailPreviewModel.financialFields) : 0 },
                            { key: "all", label: "全部字段", count: cacheDetailPreviewModel ? modelCount(previewFields()) : 0 }
                        ]

                        delegate: Button {
                            text: modelData.label + " (" + modelData.count + ")"
                            enabled: cacheDetailPreviewModel && (modelData.key === "all" || modelData.count > 0)
                            background: Rectangle {
                                radius: 6
                                color: previewFieldGroup() === modelData.key ? "#1d4ed8" : "#0f172a"
                                border.width: 1
                                border.color: previewFieldGroup() === modelData.key ? "#60a5fa" : "#243244"
                            }
                            contentItem: Text {
                                text: parent.text
                                color: "white"
                                font.pixelSize: 11
                                font.bold: previewFieldGroup() === modelData.key
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: applyPreviewFilters(modelData.key)
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: selectedPreviewDate() !== "" ? ("当前单日: " + selectedPreviewDate()) : ""
                        font.pixelSize: 11
                        color: "#fbbf24"
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: cacheDetailPreviewModel && modelCount(previewDates()) > 0

                    Text {
                        text: "点击日期"
                        font.pixelSize: 11
                        color: "#94a3b8"
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        clip: true

                        Row {
                            spacing: 6

                            Repeater {
                                model: previewDates()

                                delegate: Button {
                                    property string dateValue: String(modelData || "")
                                    text: dateValue
                                    background: Rectangle {
                                        radius: 14
                                        color: selectedPreviewDate() === dateValue ? "#1d4ed8" : "#0f172a"
                                        border.width: 1
                                        border.color: selectedPreviewDate() === dateValue ? "#60a5fa" : "#243244"
                                    }
                                    contentItem: Text {
                                        text: parent.text
                                        color: "white"
                                        font.pixelSize: 11
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    onClicked: {
                                        if (!cacheDetailPreviewModel) {
                                            return
                                        }
                                        if (selectedPreviewDate() === dateValue) {
                                            cacheDetailPreviewModel.clearSelectedDate()
                                        } else {
                                            cacheDetailPreviewModel.selectDate(dateValue)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Button {
                        text: "清除单日"
                        enabled: cacheDetailPreviewModel && selectedPreviewDate() !== ""
                        onClicked: cacheDetailPreviewModel.clearSelectedDate()
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

                        Text {
                            Layout.fillWidth: true
                            visible: previewEmptyMessage() !== ""
                            text: previewEmptyMessage()
                            font.pixelSize: 12
                            color: "#94a3b8"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.WordWrap
                        }

                        ScrollView {
                            id: previewTableViewport
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            visible: previewEmptyMessage() === ""

                            contentWidth: totalColumnWidth()
                            contentHeight: previewTableColumn.implicitHeight

                            Column {
                                id: previewTableColumn
                                width: totalColumnWidth()
                                spacing: 0

                                Row {
                                    width: parent.width
                                    height: 34
                                    spacing: 0

                                    Repeater {
                                        model: previewFields()

                                        delegate: Rectangle {
                                            property string fieldName: String(modelData || "")
                                            width: columnWidth(fieldName)
                                            height: 34
                                            color: "#111827"
                                            border.width: 1
                                            border.color: "#243244"

                                            Text {
                                                anchors.fill: parent
                                                anchors.leftMargin: 8
                                                anchors.rightMargin: 8
                                                horizontalAlignment: Text.AlignLeft
                                                verticalAlignment: Text.AlignVCenter
                                                text: fieldName
                                                font.pixelSize: 11
                                                font.bold: true
                                                color: "#e2e8f0"
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                }

                                Repeater {
                                    model: previewRows()

                                    delegate: Rectangle {
                                        property var rowData: modelData
                                        width: previewTableColumn.width
                                        height: 36
                                        color: index % 2 === 0 ? "#0f172a" : "#111827"
                                        border.width: 1
                                        border.color: "transparent"

                                        Row {
                                            anchors.fill: parent
                                            spacing: 0

                                            Repeater {
                                                model: previewFields()

                                                delegate: Rectangle {
                                                    property string fieldName: String(modelData || "")
                                                    property string dateToken: extractDateToken(rowData ? rowData[fieldName] : undefined)
                                                    width: columnWidth(fieldName)
                                                    height: 36
                                                    color: "transparent"

                                                    Text {
                                                        anchors.fill: parent
                                                        anchors.leftMargin: 8
                                                        anchors.rightMargin: 8
                                                        horizontalAlignment: Text.AlignLeft
                                                        verticalAlignment: Text.AlignVCenter
                                                        elide: Text.ElideRight
                                                        text: formatFieldValue(rowData ? rowData[fieldName] : undefined)
                                                        font.pixelSize: 11
                                                        color: dateToken !== "" ? "#93c5fd" : "#e5e7eb"
                                                    }

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        enabled: cacheDetailPreviewModel && dateToken !== ""
                                                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                        onClicked: {
                                                            if (selectedPreviewDate() === dateToken) {
                                                                cacheDetailPreviewModel.clearSelectedDate()
                                                            } else {
                                                                cacheDetailPreviewModel.selectDate(dateToken)
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
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        text: "上一页"
                        enabled: cacheDetailPreviewModel && cacheDetailPreviewModel.hasPreviousPage
                        onClicked: cacheDetailPreviewModel.previousPage()
                    }

                    Button {
                        text: "下一页"
                        enabled: cacheDetailPreviewModel && cacheDetailPreviewModel.hasNextPage
                        onClicked: cacheDetailPreviewModel.nextPage()
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: cacheDetailPreviewModel ? ("当前页 " + cacheDetailPreviewModel.currentPage + " / " + cacheDetailPreviewModel.totalPages) : ""
                        font.pixelSize: 11
                        color: "#94a3b8"
                    }
                }
            }
        }
    }

    FileDialog {
        id: exportFileDialog
        title: pendingExportFormat === "csv" ? "导出当前筛选结果为 CSV" : "导出当前筛选结果为 JSON"
        fileMode: FileDialog.SaveFile
        onAccepted: {
            if (controller) {
                controller.exportCurrentCacheDetailPreview(String(selectedFile), pendingExportFormat)
            }
        }
    }
}
