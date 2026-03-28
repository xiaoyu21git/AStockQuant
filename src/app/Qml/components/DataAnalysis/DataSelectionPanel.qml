import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".." as SharedComponents

Item {
    id: root
    width: parent ? parent.width : implicitWidth
    implicitHeight: panelColumn.implicitHeight

    property int dataSourceCount: 0

    property alias providerComboBox: providerComboBox
    property alias startDatePicker: startDatePicker
    property alias endDatePicker: endDatePicker
    property alias marketComboBox: marketComboBox
    property alias indexComboBox: indexComboBox
    property alias indexListModel: indexListModel
    property alias dataTypeCardsFlow: dataTypeCardsFlow
    property alias selectedTagsFlow: selectedTagsFlow

    signal queryRequested()
    signal providerChosen(string provider)
    signal statusRequested(string message, string type)

    function notifyStatus(message, type) {
        root.statusRequested(message, type || "info")
    }

    Column {
        id: panelColumn
        width: parent.width
        spacing: 10

        Row {
            width: parent.width
            spacing: 10

            Text {
                text: "数据源管理"
                font.pixelSize: 16
                font.bold: true
                color: "white"
            }

            Item {
                width: Math.max(0, parent.width - childrenRect.width - 20)
                height: 1
            }

            Text {
                text: dataSourceCount > 0 ? "已接入 " + dataSourceCount + " 个数据源" : "等待查询"
                font.pixelSize: 12
                color: dataSourceCount > 0 ? "#10b981" : "#a0aec0"
            }
        }

        Rectangle {
            width: parent.width
            color: "#2d3748"
            radius: 6
            implicitHeight: dataSourceConfigColumn.implicitHeight + 30

            Column {
                id: dataSourceConfigColumn
                anchors.fill: parent
                anchors.margins: 15
                spacing: 10

                Row {
                    width: parent.width
                    spacing: 10

                    Column {
                        width: (parent.width - 20) / 3
                        spacing: 4

                        Text {
                            text: "数据源"
                            font.pixelSize: 12
                            color: "#a0aec0"
                        }

                        ComboBox {
                            id: providerComboBox
                            width: parent.width
                            height: 32
                            model: ["掘金数据", "米筐数据", "聚宽数据", "TuShare", "东方财富", "自定义API"]
                            currentIndex: 0
                            onActivated: root.providerChosen(currentText)

                            background: Rectangle {
                                radius: 4
                                border.width: 1
                                border.color: providerComboBox.hovered ? "#3b82f6" : "#4b5563"
                                color: "#374151"
                            }

                            contentItem: Text {
                                text: providerComboBox.currentText
                                color: "white"
                                font.pixelSize: 13
                                leftPadding: 8
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Column {
                        width: (parent.width - 20) / 3
                        spacing: 4

                        Text {
                            text: "开始日期"
                            font.pixelSize: 12
                            color: "#a0aec0"
                        }

                        SharedComponents.DatePicker {
                            id: startDatePicker
                            width: parent.width
                            height: 32
                            placeholder: "YYYY-MM-DD"
                            selectedDate: Qt.formatDate(new Date(new Date().getFullYear() - 1, new Date().getMonth(), new Date().getDate()), "yyyy-MM-dd")
                        }
                    }

                    Column {
                        width: (parent.width - 20) / 3
                        spacing: 4

                        Text {
                            text: "结束日期"
                            font.pixelSize: 12
                            color: "#a0aec0"
                        }

                        SharedComponents.DatePicker {
                            id: endDatePicker
                            width: parent.width
                            height: 32
                            placeholder: "YYYY-MM-DD"
                            selectedDate: Qt.formatDate(new Date(), "yyyy-MM-dd")
                        }
                    }
                }

                RowLayout {
                    width: parent.width
                    spacing: 10

                    ColumnLayout {
                        Layout.preferredWidth: 180
                        Layout.maximumWidth: 180
                        spacing: 4

                        Text {
                            text: "交易所"
                            font.pixelSize: 12
                            color: "#a0aec0"
                        }

                        ComboBox {
                            id: marketComboBox
                            Layout.fillWidth: true
                            height: 32
                            model: ["上交所", "深交所", "北交所", "港股", "美股"]
                            currentIndex: 0

                            background: Rectangle {
                                radius: 4
                                border.width: 1
                                border.color: marketComboBox.hovered ? "#3b82f6" : "#4b5563"
                                color: "#374151"
                            }

                            contentItem: Text {
                                text: marketComboBox.currentText
                                color: "white"
                                font.pixelSize: 13
                                leftPadding: 8
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: "指数选择"
                            font.pixelSize: 12
                            color: "#a0aec0"
                        }

                        ComboBox {
                            id: indexComboBox
                            Layout.fillWidth: true
                            height: 32
                            textRole: "displayName"
                            currentIndex: 0
                            model: ListModel {
                                id: indexListModel
                                ListElement { displayName: "沪深300"; symbol: "000300.SH" }
                                ListElement { displayName: "中证500"; symbol: "000905.SH" }
                                ListElement { displayName: "上证50"; symbol: "000016.SH" }
                                ListElement { displayName: "创业板指"; symbol: "399006.SZ" }
                                ListElement { displayName: "中证1000"; symbol: "000852.SH" }
                                ListElement { displayName: "指数大盘股"; symbol: "BIG_CAP" }
                                ListElement { displayName: "指数小盘股"; symbol: "SMALL_CAP" }
                            }

                            background: Rectangle {
                                radius: 4
                                border.width: 1
                                border.color: indexComboBox.hovered ? "#3b82f6" : "#4b5563"
                                color: "#374151"
                            }

                            contentItem: Text {
                                text: indexComboBox.currentText
                                color: "white"
                                font.pixelSize: 13
                                leftPadding: 8
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.preferredWidth: 120
                        Layout.maximumWidth: 120
                        spacing: 4

                        Text {
                            text: "操作"
                            font.pixelSize: 12
                            color: "#a0aec0"
                        }

                        Button {
                            text: "查询"
                            Layout.fillWidth: true
                            height: 32
                            onClicked: root.queryRequested()

                            background: Rectangle {
                                color: "#3b82f6"
                                radius: 4
                            }

                            contentItem: Text {
                                text: parent.text
                                color: "white"
                                font.pixelSize: 12
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: 8

                    Column {
                        id: dataTypeCardsFlow
                        width: parent.width
                        spacing: 8

                        property var dataTypeModels: [
                            { id: "kline_daily", name: "日线", icon: "📈", color: "#3b82f6" },
                            { id: "kline_weekly", name: "周线", icon: "📊", color: "#10b981" },
                            { id: "kline_monthly", name: "月线", icon: "📉", color: "#8b5cf6" },
                            { id: "minute_data", name: "分钟", icon: "⏰", color: "#f59e0b" },
                            { id: "realtime", name: "实时", icon: "⚡", color: "#ef4444" },
                            { id: "historical", name: "历史", icon: "📜", color: "#6366f1" },
                            { id: "news", name: "舆情", icon: "🗞️", color: "#ec4899" },
                            { id: "financial", name: "财务", icon: "💰", color: "#14b8a6" },
                            { id: "policy", name: "政策", icon: "📋", color: "#f97316" },
                            { id: "alternative", name: "另类", icon: "🔮", color: "#a855f7" },
                            { id: "index", name: "指数", icon: "📊", color: "#06b6d4" },
                            { id: "derivatives", name: "衍生品", icon: "📦", color: "#84cc16" }
                        ]
                        property var selectedDataTypes: []
                        property int selectedDataTypesCount: selectedDataTypes ? selectedDataTypes.length : 0

                        function getDataTypeById(id) {
                            for (var i = 0; i < dataTypeModels.length; i++) {
                                if (dataTypeModels[i].id === id) {
                                    return dataTypeModels[i]
                                }
                            }
                            return { id: id, name: "未知类型", icon: "?", color: "#6b7280" }
                        }

                        function getDataTypeName(id) {
                            return getDataTypeById(id).name
                        }

                        function isDataTypeSelected(id) {
                            return selectedDataTypes && selectedDataTypes.indexOf(id) >= 0
                        }

                        function toggleDataType(id) {
                            var nextSelection = []
                            var removed = false

                            for (var i = 0; i < selectedDataTypes.length; i++) {
                                if (selectedDataTypes[i] === id) {
                                    removed = true
                                    continue
                                }
                                nextSelection.push(selectedDataTypes[i])
                            }

                            if (!removed) {
                                nextSelection.push(id)
                                root.notifyStatus("已选择: " + getDataTypeName(id))
                            } else {
                                root.notifyStatus("已取消选择: " + getDataTypeName(id))
                            }

                            selectedDataTypes = nextSelection
                        }

                        RowLayout {
                            width: parent.width
                            spacing: 10

                            Text {
                                text: "数据类型（可多选）"
                                font.pixelSize: 12
                                font.bold: true
                                color: "#a0aec0"
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Text {
                                text: "已选择 " + dataTypeCardsFlow.selectedDataTypesCount + " 项"
                                font.pixelSize: 11
                                color: dataTypeCardsFlow.selectedDataTypesCount > 0 ? "#3b82f6" : "#9ca3af"
                            }
                        }

                        Flow {
                            width: parent.width
                            spacing: 8

                            Repeater {
                                model: dataTypeCardsFlow.dataTypeModels

                                Rectangle {
                                    id: dataTypeCard
                                    width: 135
                                    height: 42
                                    radius: 6
                                    color: dataTypeCardsFlow.isDataTypeSelected(modelData.id)
                                           ? Qt.lighter(modelData.color, 1.35)
                                           : (dataTypeCardMouseArea.containsMouse ? "#243247" : "#1a2538")
                                    border.width: dataTypeCardsFlow.isDataTypeSelected(modelData.id) ? 2 : 1
                                    border.color: dataTypeCardsFlow.isDataTypeSelected(modelData.id) ? modelData.color : "#4b5563"

                                    MouseArea {
                                        id: dataTypeCardMouseArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: dataTypeCardsFlow.toggleDataType(modelData.id)
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        spacing: 6

                                        Text {
                                            text: modelData.icon
                                            font.pixelSize: 14
                                            color: "white"
                                        }

                                        Text {
                                            text: modelData.name
                                            font.pixelSize: 12
                                            font.bold: true
                                            color: "white"
                                            Layout.fillWidth: true
                                        }

                                        Rectangle {
                                            width: 12
                                            height: 12
                                            radius: 6
                                            color: dataTypeCardsFlow.isDataTypeSelected(modelData.id) ? modelData.color : "transparent"
                                            border.width: 1
                                            border.color: dataTypeCardsFlow.isDataTypeSelected(modelData.id) ? modelData.color : "#9ca3af"

                                            Text {
                                                anchors.centerIn: parent
                                                text: "✓"
                                                color: "white"
                                                font.pixelSize: 9
                                                font.bold: true
                                                visible: dataTypeCardsFlow.isDataTypeSelected(modelData.id)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Flow {
                        id: selectedTagsFlow
                        width: parent.width
                        spacing: 6
                        visible: dataTypeCardsFlow.selectedDataTypesCount > 0

                        Repeater {
                            model: dataTypeCardsFlow.selectedDataTypes

                            Rectangle {
                                property var dataTypeInfo: dataTypeCardsFlow.getDataTypeById(modelData)
                                height: 24
                                radius: 12
                                color: Qt.lighter(dataTypeInfo.color, 1.3)
                                implicitWidth: selectedTagRow.implicitWidth + 12

                                Row {
                                    id: selectedTagRow
                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    anchors.rightMargin: 6
                                    spacing: 4

                                    Text {
                                        text: parent.parent.dataTypeInfo.icon
                                        font.pixelSize: 10
                                        color: "white"
                                    }

                                    Text {
                                        text: parent.parent.dataTypeInfo.name
                                        font.pixelSize: 11
                                        color: "white"
                                    }

                                    MouseArea {
                                        width: 12
                                        height: 12
                                        anchors.verticalCenter: parent.verticalCenter
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: dataTypeCardsFlow.toggleDataType(modelData)

                                        Text {
                                            anchors.centerIn: parent
                                            text: "×"
                                            color: "#e5e7eb"
                                            font.pixelSize: 10
                                            font.bold: true
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
