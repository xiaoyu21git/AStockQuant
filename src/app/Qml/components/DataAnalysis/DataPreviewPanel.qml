import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property var previewModel: null
    property var selectedDataTypes: []
    property var categoryTabs: []
    property var activeColumns: []
    property int selectedTabIndex: 0
    property bool selectionSyncing: false

    signal rowClicked(var rowData)

    color: "#111827"
    radius: 10
    border.width: 1
    border.color: "#334155"

    implicitHeight: previewColumn.implicitHeight + 24

    function normalizeList(value) {
        var normalized = []
        var seen = {}
        if (!value) {
            return normalized
        }

        var items = []
        if (Array.isArray(value)) {
            items = value
        } else if (value.length !== undefined) {
            for (var i = 0; i < value.length; ++i) {
                items.push(value[i])
            }
        } else {
            items = [value]
        }

        for (var j = 0; j < items.length; ++j) {
            var item = String(items[j] || "").trim()
            if (!item || seen[item]) {
                continue
            }
            seen[item] = true
            normalized.push(item)
        }

        return normalized
    }

    function dataTypeLabel(dataType) {
        var normalized = String(dataType || "").trim()
        switch (normalized) {
        case "kline_daily":
        case "kline":
            return "日线"
        case "kline_weekly":
            return "周线"
        case "kline_monthly":
            return "月线"
        case "minute_data":
            return "分钟"
        case "realtime":
            return "实时"
        case "historical":
            return "历史"
        case "financial":
            return "财务"
        case "news":
            return "舆情"
        case "policy":
            return "政策"
        case "alternative":
            return "另类"
        case "index":
            return "指数"
        case "derivatives":
            return "衍生品"
        case "清洗结果":
            return "清洗结果"
        default:
            return normalized || "其他"
        }
    }

    function categoryKind(dataType) {
        var normalized = String(dataType || "").trim()
        switch (normalized) {
        case "kline_daily":
        case "kline_weekly":
        case "kline_monthly":
        case "minute_data":
        case "realtime":
        case "historical":
            return "kline"
        case "financial":
            return "financial"
        case "news":
            return "news"
        case "policy":
            return "policy"
        case "alternative":
            return "alternative"
        case "index":
            return "index"
        case "derivatives":
            return "derivatives"
        default:
            return "other"
        }
    }

    function knownCategories() {
        return [
            "kline_daily",
            "kline_weekly",
            "kline_monthly",
            "minute_data",
            "realtime",
            "historical",
            "financial",
            "news",
            "policy",
            "alternative",
            "index",
            "derivatives",
            "清洗结果"
        ]
    }

    function currentCategoryKey() {
        if (!previewModel) {
            return ""
        }

        return String(previewModel.currentCategory || "").trim()
    }

    function buildCategoryTabs() {
        var categories = []
        var seen = {}
        var candidates = normalizeList(selectedDataTypes)
        var currentCategory = currentCategoryKey()

        if (currentCategory) {
            candidates.unshift(currentCategory)
        }

        var fallbackCategories = knownCategories()
        for (var i = 0; i < fallbackCategories.length; ++i) {
            candidates.push(fallbackCategories[i])
        }

        for (var j = 0; j < candidates.length; ++j) {
            var category = String(candidates[j] || "").trim()
            if (!category || seen[category] || !previewModel) {
                continue
            }

            var count = previewModel.categoryCount(category)
            if (count <= 0) {
                continue
            }

            seen[category] = true
            categories.push({
                key: category,
                label: dataTypeLabel(category),
                count: count,
                kind: categoryKind(category)
            })
        }

        if (categories.length === 0 && previewModel && currentCategory) {
            var currentCount = previewModel.categoryCount(currentCategory)
            if (currentCount > 0) {
                categories.push({
                    key: currentCategory,
                    label: dataTypeLabel(currentCategory),
                    count: currentCount,
                    kind: categoryKind(currentCategory)
                })
            }
        }

        categoryTabs = categories
        if (selectedTabIndex >= categoryTabs.length) {
            selectedTabIndex = categoryTabs.length > 0 ? 0 : -1
        }
    }

    function columnsForCategory(category) {
        var kind = categoryKind(category)
        if (kind === "kline") {
            return [
                { key: "code", label: "代码", width: 96, align: "left" },
                { key: "name", label: "名称", width: 112, align: "left" },
                { key: "date", label: "日期", width: 110, align: "left" },
                { key: "source", label: "来源", width: 92, align: "left" },
                { key: "dataType", label: "类型", width: 92, align: "left" },
                { key: "open", label: "开盘", width: 84, align: "right", format: "number" },
                { key: "high", label: "最高", width: 84, align: "right", format: "number" },
                { key: "low", label: "最低", width: 84, align: "right", format: "number" },
                { key: "close", label: "收盘", width: 84, align: "right", format: "number" },
                { key: "change", label: "涨跌幅", width: 88, align: "right", format: "percent" },
                { key: "volume", label: "成交量", width: 104, align: "right", format: "volume" },
                { key: "recordCount", label: "记录数", width: 86, align: "right", format: "int" }
            ]
        }

        return [
            { key: "code", label: "代码", width: 110, align: "left" },
            { key: "name", label: "名称", width: 128, align: "left" },
            { key: "date", label: kind === "financial" ? "报告期" : "日期", width: 120, align: "left" },
            { key: "timeRange", label: "区间", width: 150, align: "left" },
            { key: "source", label: "来源", width: 104, align: "left" },
            { key: "dataType", label: "类型", width: 104, align: "left" },
            { key: "recordCount", label: "记录数", width: 88, align: "right", format: "int" }
        ]
    }

    function activeCategory() {
        if (categoryTabs.length <= 0) {
            return currentCategoryKey()
        }

        if (selectedTabIndex < 0 || selectedTabIndex >= categoryTabs.length) {
            return categoryTabs[0].key
        }

        return categoryTabs[selectedTabIndex].key
    }

    function isPreferableImmediateCategory(category) {
        var normalized = String(category || "").trim()
        if (!normalized) {
            return false
        }

        if (normalized === "kline" || normalized === "kline_daily") {
            return false
        }

        return categoryKind(normalized) !== "kline"
    }

    function syncSelection() {
        if (selectionSyncing) {
            return
        }

        selectionSyncing = true
        buildCategoryTabs()
        if (!previewModel || categoryTabs.length === 0) {
            activeColumns = columnsForCategory(currentCategoryKey())
            selectionSyncing = false
            return
        }

        var current = currentCategoryKey()
        var targetIndex = -1
        if (current) {
            for (var i = 0; i < categoryTabs.length; ++i) {
                if (categoryTabs[i].key === current) {
                    targetIndex = i
                    break
                }
            }
        }

        if (targetIndex < 0) {
            for (var j = 0; j < categoryTabs.length; ++j) {
                if (isPreferableImmediateCategory(categoryTabs[j].key)) {
                    targetIndex = j
                    break
                }
            }
        }

        if (targetIndex < 0) {
            for (var k = 0; k < categoryTabs.length; ++k) {
                if (categoryTabs[k].key !== "kline") {
                    targetIndex = k
                    break
                }
            }
        }

        if (targetIndex < 0) {
            targetIndex = 0
        }

        selectedTabIndex = targetIndex
        if (tabBar.currentIndex !== targetIndex) {
            tabBar.currentIndex = targetIndex
        }
        var targetCategory = categoryTabs[targetIndex].key
        activeColumns = columnsForCategory(targetCategory)
        selectionSyncing = false
    }

    function handleTabSelection(index) {
        if (selectionSyncing || !previewModel || index < 0 || index >= categoryTabs.length) {
            return
        }

        selectionSyncing = true
        selectedTabIndex = index
        if (tabBar.currentIndex !== index) {
            tabBar.currentIndex = index
        }
        var targetCategory = categoryTabs[index].key
        activeColumns = columnsForCategory(targetCategory)
        if (previewModel.currentCategory !== targetCategory) {
            previewModel.currentCategory = targetCategory
        }
        selectionSyncing = false
    }

    function formatNumber(value, digits) {
        var number = Number(value)
        if (isNaN(number)) {
            return "—"
        }
        return number.toFixed(digits)
    }

    function formatVolume(value) {
        var number = Number(value)
        if (isNaN(number)) {
            return "—"
        }
        return number.toLocaleString()
    }

    function cellText(rowData, column) {
        if (!rowData || rowData[column.key] === undefined || rowData[column.key] === null || rowData[column.key] === "") {
            return "—"
        }

        var value = rowData[column.key]
        switch (column.format) {
        case "number":
            return formatNumber(value, 2)
        case "percent":
            return formatNumber(value, 2) + "%"
        case "volume":
            return formatVolume(value)
        case "int":
            return String(Number(value))
        default:
            return String(value)
        }
    }

    function columnsTotalWidth(columns) {
        var total = 0
        for (var i = 0; i < columns.length; ++i) {
            total += Number(columns[i].width || 0)
        }
        return total
    }

    function rowDataAt(index) {
        if (!previewModel || index < 0 || index >= previewModel.count) {
            return null
        }

        if (previewModel.getRow) {
            return previewModel.getRow(index)
        }

        return null
    }

    Component.onCompleted: syncSelection()

    onPreviewModelChanged: syncSelection()
    onSelectedDataTypesChanged: syncSelection()

    Connections {
        target: previewModel

        function onCategoryCountsChanged() {
            syncSelection()
        }

        function onCurrentCategoryChanged() {
            syncSelection()
        }

        function onDataUpdated() {
            syncSelection()
        }
    }

    ColumnLayout {
        id: previewColumn
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: "📋 数据预览"
                    font.pixelSize: 16
                    font.bold: true
                    color: "white"
                }

                Text {
                    text: previewModel ? previewModel.pageSummary : "等待数据加载"
                    font.pixelSize: 11
                    color: "#94a3b8"
                }
            }

            Text {
                text: previewModel ? (previewModel.totalCount + " 条") : "0 条"
                font.pixelSize: 12
                font.bold: true
                color: "#38bdf8"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            radius: 10
            color: "#0f172a"
            border.width: 1
            border.color: "#1f2937"
            implicitHeight: tabBar.implicitHeight + 18

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                TabBar {
                    id: tabBar
                    Layout.fillWidth: true
                    visible: categoryTabs.length > 0

                    onCurrentIndexChanged: {
                        if (currentIndex >= 0 && currentIndex < categoryTabs.length) {
                            root.handleTabSelection(currentIndex)
                        }
                    }

                    Repeater {
                        model: categoryTabs

                        TabButton {
                            id: tabButton
                            padding: 0
                            implicitWidth: tabText.implicitWidth + 28
                            implicitHeight: 34

                            background: Rectangle {
                                radius: 10
                                color: tabBar.currentIndex === index ? "#2563eb" : "#1f2937"
                                border.width: 1
                                border.color: tabBar.currentIndex === index ? "#60a5fa" : "#334155"
                            }

                            contentItem: Text {
                                id: tabText
                                text: modelData.label + " (" + modelData.count + ")"
                                color: "white"
                                font.pixelSize: 12
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            onClicked: root.handleTabSelection(index)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "当前类别: " + dataTypeLabel(activeCategory())
                        font.pixelSize: 11
                        color: "#93c5fd"
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: "上一页"
                        enabled: previewModel && previewModel.hasPreviousPage
                        onClicked: previewModel.previousPage()
                    }

                    Button {
                        text: "下一页"
                        enabled: previewModel && previewModel.hasNextPage
                        onClicked: previewModel.nextPage()
                    }
                }
            }
        }

        Rectangle {
            id: tableCard
            Layout.fillWidth: true
            Layout.preferredHeight: 340
            radius: 10
            color: "#0b1220"
            border.width: 1
            border.color: "#1f2937"

            Flickable {
                id: tableFlickable
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                contentWidth: tableContent.implicitWidth
                contentHeight: tableContent.implicitHeight

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                Column {
                    id: tableContent
                    width: Math.max(tableFlickable.width, root.columnsTotalWidth(root.activeColumns))
                    spacing: 0

                    Rectangle {
                        width: tableContent.width
                        height: 40
                        radius: 8
                        color: "#111827"
                        border.width: 1
                        border.color: "#334155"

                        Row {
                            anchors.fill: parent
                            Repeater {
                                model: root.activeColumns

                                delegate: Rectangle {
                                    width: modelData.width
                                    height: parent.height
                                    color: "transparent"

                                    Text {
                                        text: modelData.label
                                        anchors.centerIn: parent
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: "#e2e8f0"
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }

                    Item {
                        width: 1
                        height: 6
                    }

                    Repeater {
                        model: previewModel ? previewModel.count : 0

                        delegate: Rectangle {
                            id: rowDelegate
                            width: tableContent.width
                            height: 38
                            radius: 6
                            color: index % 2 === 0 ? "#0f172a" : "#111827"
                            border.width: 1
                            border.color: hoveredMouseArea.containsMouse ? "#334155" : "transparent"

                            property var rowData: root.rowDataAt(index)
                            property bool hovered: false
                            property real changeValue: rowData && rowData.change !== undefined && rowData.change !== null ? Number(rowData.change) : NaN

                            MouseArea {
                                id: hoveredMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor

                                onClicked: root.rowClicked(parent.rowData)
                                onEntered: parent.hovered = true
                                onExited: parent.hovered = false
                            }

                            Row {
                                anchors.fill: parent
                                Repeater {
                                    model: root.activeColumns

                                    delegate: Rectangle {
                                        width: modelData.width
                                        height: parent.height
                                        color: "transparent"

                                        Text {
                                            anchors.left: parent.left
                                            anchors.leftMargin: 8
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: parent.width - 16
                                            text: root.cellText(rowDelegate.rowData, modelData)
                                            font.pixelSize: 12
                                            color: modelData.key === "change" ? (rowDelegate.changeValue > 0 ? "#ef4444" : (rowDelegate.changeValue < 0 ? "#34d399" : "#e5e7eb")) : "#e5e7eb"
                                            horizontalAlignment: modelData.align === "right" ? Text.AlignRight : Text.AlignLeft
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        visible: !previewModel || previewModel.count === 0
                        width: tableContent.width
                        height: 96
                        radius: 8
                        color: "#0f172a"
                        border.width: 1
                        border.color: "#334155"

                        Text {
                            anchors.centerIn: parent
                            text: previewModel ? "当前标签暂无可显示数据" : "暂无预览数据"
                            font.pixelSize: 13
                            color: "#94a3b8"
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: previewModel ? ("当前页 " + previewModel.currentPage + " / " + previewModel.totalPages) : "第 0 / 0 页"
                font.pixelSize: 11
                color: "#94a3b8"
            }

            Item { Layout.fillWidth: true }

            Text {
                text: previewModel ? previewModel.pageSummary : ""
                font.pixelSize: 11
                color: "#64748b"
            }
        }
    }
}