import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.settings 1.0
import "." as WR
import "../../components/DynamicWorkspace" as Workspace
import "../../components" as AppComponents

Item {
    id: root

    // ============ 持久化 ============
    Settings {
        id: layoutSettings
        category: "DynamicWorkspace"
        property string savedLayout: ""
    }

    Component.onCompleted: {
        if (layoutSettings.savedLayout) {
            root.loadLayout(layoutSettings.savedLayout)
        }
    }

    // ============ 颜色常量 ============
    readonly property color pageBg: "#0F172A"
    readonly property color toolbarBg: "#1E293B"
    readonly property color cardBg: "#1E293B"
    readonly property color cardBorder: "#334155"
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color textTertiary: "#64748B"
    readonly property color accent: "#3B82F6"

    // ============ 数据模型 ============
    property var widgetRegistry: WR.WidgetRegistry

    ListModel {
        id: workspaceModel
        // { instanceId, typeName, title, colSpan, rowSpan, widgetConfig }
    }

    // ============ 布局 ============
    Rectangle {
        anchors.fill: parent
        color: root.pageBg

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            // --- 工具栏 ---
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                color: root.toolbarBg
                radius: 8
                border.color: root.cardBorder
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 8

                    // 标题
                    Text {
                        text: "动态工作区"
                        color: root.textPrimary
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }

                    Item { Layout.fillWidth: true }

                    // [+ 添加控件]
                    AppComponents.ButtonPrimary {
                        buttonText: "+ 添加控件"
                        implicitWidth: 110
                        implicitHeight: 34
                        onClicked: widgetPalette.open()
                    }

                    // 保存布局
                    AppComponents.ButtonSecondary {
                        buttonText: "保存布局"
                        implicitWidth: 90
                        implicitHeight: 34
                        onClicked: root.saveLayout()
                    }

                    // 加载布局
                    AppComponents.ButtonSecondary {
                        buttonText: "加载布局"
                        implicitWidth: 90
                        implicitHeight: 34
                        onClicked: root.loadLayout(root.lastSavedLayout)
                    }

                    // 重置
                    AppComponents.ButtonSecondary {
                        buttonText: "重置"
                        implicitWidth: 70
                        implicitHeight: 34
                        onClicked: root.resetLayout()
                    }
                }
            }

            // --- 画布 ---
            Workspace.WidgetCanvas {
                id: widgetCanvas
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: workspaceModel
                onWidgetRemoveRequest: function(instanceId) {
                    root.removeWidget(instanceId)
                }
                onWidgetConfigRequest: function(instanceId) {
                    // Phase 2: 控件配置
                    console.log("DynamicWorkspace: config requested for", instanceId)
                }
                onWidgetReorderRequest: function(fromIndex, toIndex) {
                    root.moveWidget(fromIndex, toIndex)
                }
                onWidgetResizeRequest: function(instanceId, dCol, dRow) {
                    root.resizeWidget(instanceId, dCol, dRow)
                }
                onWidgetMergeRequest: function(fromIndex, toIndex, side) {
                    root.mergeWidget(fromIndex, toIndex, side)
                }
            }
        }
    }

    // --- 控件选择面板 ---
    Workspace.WidgetPalette {
        id: widgetPalette
        registry: root.widgetRegistry
        onWidgetSelected: function(typeName) {
            root.addWidget(typeName)
            widgetPalette.close()
        }
    }

    // ============ 方法 ============

    property string lastSavedLayout: ""

    // 防抖持久化: 每次变更后 500ms 自动写入磁盘
    Timer {
        id: persistTimer
        interval: 500
        repeat: false
        onTriggered: {
            layoutSettings.savedLayout = root.saveLayout()
        }
    }

    function schedulePersist() {
        persistTimer.restart()
    }

    function generateInstanceId() {
        return "widget_" + Date.now() + "_" + Math.floor(Math.random() * 10000)
    }

    function addWidget(typeName) {
        var meta = widgetRegistry.getWidgetMeta(typeName)
        if (!meta) {
            console.warn("DynamicWorkspace: unknown widget type", typeName)
            return
        }
        workspaceModel.append({
            instanceId: generateInstanceId(),
            typeName: typeName,
            title: meta.label,
            colSpan: meta.defaultColSpan,
            rowSpan: meta.defaultRowSpan,
            widgetConfig: ({})
        })
        Qt.callLater(widgetCanvas.relayout)
        root.schedulePersist()
    }

    function removeWidget(instanceId) {
        for (var i = 0; i < workspaceModel.count; i++) {
            if (workspaceModel.get(i).instanceId === instanceId) {
                workspaceModel.remove(i)
                break
            }
        }
        Qt.callLater(widgetCanvas.relayout)
        root.schedulePersist()
    }

    function mergeWidget(fromIndex, toIndex, side) {
        if (fromIndex < 0 || toIndex < 0 || fromIndex >= workspaceModel.count || toIndex >= workspaceModel.count) return
        if (fromIndex === toIndex) return

        var fromId = workspaceModel.get(fromIndex).instanceId
        var toId = workspaceModel.get(toIndex).instanceId

        if (side === "top" || side === "bottom") {
            // 上下: 插入为新行, 保持 colSpan=12
            var insertAt = side === "top" ? toIndex : toIndex + 1
            workspaceModel.move(fromIndex, insertAt > fromIndex ? insertAt - 1 : insertAt, 1)
            // 确保全宽
            for (var i = 0; i < workspaceModel.count; i++) {
                if (workspaceModel.get(i).instanceId === fromId) {
                    workspaceModel.setProperty(i, "colSpan", 12)
                    break
                }
            }
        } else {
            // 左右: 插入同行, 平分 colSpan
            insertAt = side === "left" ? toIndex : toIndex + 1
            workspaceModel.move(fromIndex, insertAt > fromIndex ? insertAt - 1 : insertAt, 1)

            var idxA = -1, idxB = -1
            for (i = 0; i < workspaceModel.count; i++) {
                var id = workspaceModel.get(i).instanceId
                if (id === fromId) idxA = i
                if (id === toId) idxB = i
            }
            if (idxA >= 0 && idxB >= 0 && idxA !== idxB) {
                var half = Math.floor(12 / 2)
                workspaceModel.setProperty(idxA, "colSpan", half)
                workspaceModel.setProperty(idxB, "colSpan", 12 - half)
            }
        }
        Qt.callLater(widgetCanvas.relayout)
        root.schedulePersist()
    }

    function moveWidget(fromIndex, toIndex) {
        if (fromIndex < 0 || fromIndex >= workspaceModel.count
                || toIndex < 0 || toIndex >= workspaceModel.count
                || fromIndex === toIndex) {
            return
        }
        workspaceModel.move(fromIndex, toIndex, 1)
        Qt.callLater(widgetCanvas.relayout)
        root.schedulePersist()
    }

    function resizeWidget(instanceId, newColSpan, newRowSpan) {
        for (var i = 0; i < workspaceModel.count; i++) {
            if (workspaceModel.get(i).instanceId === instanceId) {
                var cs = Math.max(1, Math.min(12, newColSpan))
                var rs = Math.max(1, newRowSpan)
                if (workspaceModel.get(i).colSpan !== cs || workspaceModel.get(i).rowSpan !== rs) {
                    workspaceModel.setProperty(i, "colSpan", cs)
                    workspaceModel.setProperty(i, "rowSpan", rs)
                    Qt.callLater(widgetCanvas.relayout)
                }
                break
            }
        }
        root.schedulePersist()
    }

    function saveLayout() {
        var layout = []
        for (var i = 0; i < workspaceModel.count; i++) {
            var item = workspaceModel.get(i)
            layout.push({
                typeName: item.typeName,
                title: item.title,
                colSpan: item.colSpan,
                rowSpan: item.rowSpan,
                widgetConfig: item.widgetConfig
            })
        }
        var json = JSON.stringify(layout, null, 2)
        lastSavedLayout = json
        console.log("DynamicWorkspace: layout saved, items:", layout.length)
        return json
    }

    function loadLayout(json) {
        // 1. 异常处理
        if (!json || typeof json !== "string") {
            console.warn("DynamicWorkspace: invalid layout input")
            return false
        }
        var data
        try {
            data = JSON.parse(json)
        } catch (e) {
            console.warn("DynamicWorkspace: invalid JSON layout", e)
            return false
        }

        if (!Array.isArray(data)) {
            console.warn("DynamicWorkspace: layout must be an array")
            return false
        }
        var layout = data

        // 2. 清空
        workspaceModel.clear()

        // 3. 逐条校验并恢复
        var restored = 0
        for (var i = 0; i < layout.length; i++) {
            var item = layout[i]
            if (!item.typeName) {
                console.warn("DynamicWorkspace: skipping layout item without typeName", JSON.stringify(item))
                continue
            }
            var meta = widgetRegistry.getWidgetMeta(item.typeName)
            if (!meta) {
                console.warn("DynamicWorkspace: unknown widget type", item.typeName)
                continue
            }
            workspaceModel.append({
                instanceId: generateInstanceId(),
                typeName: item.typeName,
                title: item.title || meta.label,
                colSpan: (typeof item.colSpan === "number" && item.colSpan > 0) ? item.colSpan : meta.defaultColSpan,
                rowSpan: (typeof item.rowSpan === "number" && item.rowSpan > 0) ? item.rowSpan : meta.defaultRowSpan,
                widgetConfig: item.widgetConfig || ({})
            })
            restored++
        }
        console.log("DynamicWorkspace: layout loaded, items:", restored)
        Qt.callLater(widgetCanvas.relayout)
        root.schedulePersist()
        return true
    }

    function resetLayout() {
        if (workspaceModel.count > 0) {
            lastSavedLayout = saveLayout()
        }
        workspaceModel.clear()
        widgetCanvas.clearState()
        Qt.callLater(widgetCanvas.relayout)
        root.schedulePersist()
    }
}
