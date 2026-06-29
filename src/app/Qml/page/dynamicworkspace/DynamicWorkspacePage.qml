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
        } else {
            root.loadDefaultLayout()
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
    }

    // ============ 默认交易布局 ============
    // rowSpan 1 ≈ 120px, 按实际交易场景排列
    readonly property var defaultLayoutItems: [
        // Row 1: 账户概览全宽 (120px)
        { typeName: "account_card",     colSpan: 12, rowSpan: 1 },
        // Row 2: 左K线 + 右下单 (480px)
        { typeName: "kline_chart",      colSpan: 8, rowSpan: 4 },
        { typeName: "order_form",       colSpan: 4, rowSpan: 4 },
        // Row 3: 五档盘口全宽 (360px)
        { typeName: "depth_panel",      colSpan: 12, rowSpan: 3 },
        // Row 4: 策略监控 + 执行日志 (240px)
        { typeName: "strategy_monitor", colSpan: 6, rowSpan: 2 },
        { typeName: "execution_log",    colSpan: 6, rowSpan: 2 },
        // Row 5: 持仓列表 + 委托列表 (240px)
        { typeName: "position_list",    colSpan: 6, rowSpan: 2 },
        { typeName: "order_list",       colSpan: 6, rowSpan: 2 }
    ]

    function loadDefaultLayout() {
        workspaceModel.clear()
        for (var i = 0; i < defaultLayoutItems.length; i++) {
            var item = defaultLayoutItems[i]
            var meta = widgetRegistry.getWidgetMeta(item.typeName)
            if (!meta) continue
            workspaceModel.append({
                instanceId: generateInstanceId(),
                typeName: item.typeName,
                title: meta.label,
                colSpan: item.colSpan,
                rowSpan: item.rowSpan,
                widgetConfig: ({})
            })
        }
        Qt.callLater(function() {
            widgetCanvas.markModelChanged()
            widgetCanvas.relayout()
            widgetCanvas.scrollToTop()
        })
        root.schedulePersist()
        console.log("DynamicWorkspace: default layout loaded, items:", workspaceModel.count)
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

                    Text {
                        text: "动态工作区"
                        color: root.textPrimary
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }

                    // 自动保存状态指示
                    Text {
                        text: statusText
                        color: root.textTertiary
                        font.pixelSize: 11
                        visible: statusText.length > 0
                    }

                    Item { Layout.fillWidth: true }

                    // [+ 添加控件]
                    AppComponents.ButtonPrimary {
                        buttonText: "+ 添加控件"
                        implicitWidth: 110
                        implicitHeight: 34
                        onClicked: widgetPalette.open()
                    }

                    // 重置为默认布局
                    AppComponents.ButtonSecondary {
                        buttonText: "默认布局"
                        implicitWidth: 80
                        implicitHeight: 34
                        onClicked: root.loadDefaultLayout()
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

    // ============ 自动保存状态 ============
    property string statusText: ""
    Timer {
        id: statusClearTimer
        interval: 2000
        repeat: false
        onTriggered: statusText = ""
    }

    // ============ 方法 ============

    // 防抖持久化: 每次变更后 800ms 自动写入磁盘
    Timer {
        id: persistTimer
        interval: 800
        repeat: false
        onTriggered: {
            layoutSettings.savedLayout = root.saveLayout()
            statusText = "✓ 已自动保存"
            statusClearTimer.restart()
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
        widgetCanvas.markModelChanged()
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
        widgetCanvas.markModelChanged()
        Qt.callLater(widgetCanvas.relayout)
        root.schedulePersist()
    }

    function mergeWidget(fromIndex, toIndex, side) {
        if (fromIndex < 0 || toIndex < 0 || fromIndex >= workspaceModel.count || toIndex >= workspaceModel.count) return
        if (fromIndex === toIndex) return

        var fromId = workspaceModel.get(fromIndex).instanceId
        var toId = workspaceModel.get(toIndex).instanceId

        if (side === "top" || side === "bottom") {
            var insertAt = side === "top" ? toIndex : toIndex + 1
            workspaceModel.move(fromIndex, insertAt > fromIndex ? insertAt - 1 : insertAt, 1)
            for (var i = 0; i < workspaceModel.count; i++) {
                if (workspaceModel.get(i).instanceId === fromId) {
                    workspaceModel.setProperty(i, "colSpan", 12)
                    break
                }
            }
        } else {
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
        widgetCanvas.markModelChanged()
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
        widgetCanvas.markModelChanged()
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
                    widgetCanvas.markModelChanged()
                    Qt.callLater(widgetCanvas.relayout)
                }
                break
            }
        }
        root.schedulePersist()
    }

    // ============ 持久化核心 ============
    readonly property int layoutVersion: 2

    function saveLayout() {
        var items = []
        for (var i = 0; i < workspaceModel.count; i++) {
            var item = workspaceModel.get(i)
            items.push({
                typeName: item.typeName,
                title: item.title,
                colSpan: item.colSpan,
                rowSpan: item.rowSpan,
                widgetConfig: item.widgetConfig
            })
        }
        var wrapper = { version: layoutVersion, items: items }
        return JSON.stringify(wrapper)
    }

    function loadLayout(json) {
        if (!json || typeof json !== "string") return false
        var data
        try { data = JSON.parse(json) } catch (e) { return false }

        var layout
        if (data && typeof data.version === "number") {
            layout = data.items || []
        } else if (Array.isArray(data)) {
            layout = data
        } else {
            return false
        }

        workspaceModel.clear()
        var restored = 0
        for (var i = 0; i < layout.length; i++) {
            var item = layout[i]
            if (!item.typeName) continue
            var meta = widgetRegistry.getWidgetMeta(item.typeName)
            if (!meta) continue

            var cs = (typeof item.colSpan === "number" && item.colSpan > 0)
                ? Math.max(1, Math.min(12, item.colSpan)) : meta.defaultColSpan
            var rs = (typeof item.rowSpan === "number" && item.rowSpan > 0)
                ? Math.max(1, item.rowSpan) : meta.defaultRowSpan

            workspaceModel.append({
                instanceId: generateInstanceId(),
                typeName: item.typeName,
                title: item.title || meta.label,
                colSpan: cs,
                rowSpan: rs,
                widgetConfig: item.widgetConfig || ({})
            })
            restored++
        }
        Qt.callLater(function() {
            widgetCanvas.markModelChanged()
            widgetCanvas.relayout()
            widgetCanvas.scrollToTop()
        })
        root.schedulePersist()
        return restored > 0
    }
}
