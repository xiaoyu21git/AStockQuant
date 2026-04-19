import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../utils/CustomStockPoolStore.js" as CustomStockPoolStore

Item {
    id: root

    property string title: "关联自选股票池"
    property string helperText: "自选池用于策略/因子联动，不会覆盖回测股票池。"
    property string selectedPoolId: ""
    property string selectedPoolName: ""
    property var selectedPoolSymbols: []
    property var availablePools: []
    property bool showEmptyOption: true

    signal bindingChanged(var binding)

    implicitHeight: contentColumn.implicitHeight
    implicitWidth: contentColumn.implicitWidth

    function modelWithEmptyOption() {
        var items = []
        if (showEmptyOption) {
            items.push({
                id: "",
                name: "不关联",
                symbols: []
            })
        }

        for (var index = 0; index < availablePools.length; ++index) {
            items.push(availablePools[index])
        }
        return items
    }

    function indexOfPool(poolId) {
        var items = modelWithEmptyOption()
        for (var index = 0; index < items.length; ++index) {
            if (String(items[index].id || "") === String(poolId || "")) {
                return index
            }
        }
        return 0
    }

    function currentBinding() {
        return {
            poolId: selectedPoolId,
            poolName: selectedPoolName,
            symbols: CustomStockPoolStore.CustomStockPoolStore.normalizeSymbolList(selectedPoolSymbols || []),
            hasBinding: !!String(selectedPoolId || "").trim()
        }
    }

    function refreshPools() {
        availablePools = CustomStockPoolStore.CustomStockPoolStore.listPools()
        poolComboBox.model = modelWithEmptyOption()
        poolComboBox.currentIndex = indexOfPool(selectedPoolId)
    }

    function setBinding(poolId, poolName, symbols) {
        selectedPoolId = String(poolId || "").trim()
        selectedPoolName = String(poolName || "").trim()
        selectedPoolSymbols = CustomStockPoolStore.CustomStockPoolStore.normalizeSymbolList(symbols || [])
        poolComboBox.currentIndex = indexOfPool(selectedPoolId)
    }

    function setBindingFromEntity(entity) {
        var binding = CustomStockPoolStore.CustomStockPoolStore.extractLinkedStockPool(entity)
        setBinding(binding.poolId, binding.poolName, binding.symbols)
    }

    function clearBinding() {
        setBinding("", "", [])
        root.bindingChanged(currentBinding())
    }

    function focusSelector() {
        poolComboBox.forceActiveFocus()
    }

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: root.title
                font.pixelSize: 14
                font.weight: Font.Medium
                color: "#f1f5f9"
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                Layout.preferredWidth: 64
                Layout.preferredHeight: 28
                radius: 14
                color: "#1d4ed8"
                border.width: 1
                border.color: "#3b82f6"

                Text {
                    anchors.centerIn: parent
                    text: "刷新"
                    font.pixelSize: 12
                    color: "white"
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.refreshPools()
                }
            }
        }

        ComboBox {
            id: poolComboBox
            Layout.fillWidth: true
            textRole: "name"
            model: root.modelWithEmptyOption()

            background: Rectangle {
                implicitHeight: 38
                radius: 6
                color: "#0f172a"
                border.width: 1
                border.color: "#334155"
            }

            contentItem: Text {
                text: poolComboBox.displayText
                color: "#f1f5f9"
                font.pixelSize: 13
                padding: 10
                verticalAlignment: Text.AlignVCenter
            }

            onActivated: {
                var item = model[index]
                root.selectedPoolId = String(item && item.id || "").trim()
                root.selectedPoolName = String(item && item.name || "").trim()
                root.selectedPoolSymbols = CustomStockPoolStore.CustomStockPoolStore.normalizeSymbolList(item && item.symbols || [])
                root.bindingChanged(root.currentBinding())
            }
        }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            color: "#94a3b8"
            text: root.selectedPoolId
                ? ("当前关联: " + root.selectedPoolName + " · " + root.selectedPoolSymbols.length + " 只标的")
                : root.helperText
        }
    }

    Component.onCompleted: {
        refreshPools()
        poolComboBox.currentIndex = indexOfPool(selectedPoolId)
    }

    onVisibleChanged: {
        if (visible) {
            refreshPools()
        }
    }
}