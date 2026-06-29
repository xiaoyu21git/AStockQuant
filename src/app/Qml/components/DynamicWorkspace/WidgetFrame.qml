import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../page/dynamicworkspace" as WR

Rectangle {
    id: root

    // ============ 属性 ============
    property string instanceId: ""
    property string typeName: ""
    property string title: ""
    property var widgetConfig: ({})
    property string mergeHighlight: ""
    property int gridColSpan: 12
    property int gridRowSpan: 1

    // ============ 信号 ============
    signal closeRequest(string instanceId)
    signal configRequest(string instanceId)
    signal dragStarted(string instanceId)
    signal dragMoved(string instanceId, real globalX, real globalY)
    signal dragEnded(string instanceId)
    signal resizeRequest(string instanceId, int colSpan, int rowSpan)

    // ============ 状态 ============
    property bool hovered: false
    property bool dragActive: false
    property point dragStartPos: Qt.point(0, 0)

    // ============ 样式 ============
    color: root.dragActive ? Qt.rgba(0.23, 0.47, 0.96, 0.25) : "#1E293B"
    border.color: root.dragActive ? "#3B82F6" : (root.hovered ? "#475569" : "#334155")
    border.width: root.dragActive ? 2 : 1
    radius: 8
    clip: true

    // 拖拽中轻微缩放
    scale: root.dragActive ? 1.02 : 1.0
    z: root.dragActive ? 10 : 0

    Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
    Behavior on color { ColorAnimation { duration: 150 } }
    Behavior on border.color { ColorAnimation { duration: 150 } }

    // ============ 布局 ============
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- 标题栏 ---
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: Qt.rgba(0, 0, 0, 0.25)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 8

                // 拖拽手柄图标
                Text {
                    text: "⋮⋮"
                    color: "#64748B"
                    font.pixelSize: 14
                }

                Text {
                    text: root.title
                    color: "#F1F5F9"
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                // 设置按钮
                Rectangle {
                    width: 24
                    height: 24
                    radius: 4
                    color: settingsMa.containsMouse ? Qt.rgba(1, 1, 1, 0.1) : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "⚙"
                        color: "#94A3B8"
                        font.pixelSize: 12
                    }

                    MouseArea {
                        id: settingsMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.configRequest(root.instanceId)
                    }
                }

                // 关闭按钮
                Rectangle {
                    width: 24
                    height: 24
                    radius: 4
                    color: closeMa.containsMouse ? Qt.rgba(0.94, 0.27, 0.27, 0.2) : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: closeMa.containsMouse ? "#EF4444" : "#94A3B8"
                        font.pixelSize: 14
                    }

                    MouseArea {
                        id: closeMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.closeRequest(root.instanceId)
                    }
                }
            }

            // --- 拖拽手势（覆盖整个标题栏） ---
            MouseArea {
                id: dragMa
                anchors.fill: parent
                cursorShape: root.dragActive ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                preventStealing: true

                onPressed: function(mouse) {
                    root.dragStartPos = Qt.point(mouse.x, mouse.y)
                    root.dragActive = false
                    mouse.accepted = true
                }

                onPositionChanged: function(mouse) {
                    if (!root.dragActive) {
                        var dx = mouse.x - root.dragStartPos.x
                        var dy = mouse.y - root.dragStartPos.y
                        if (Math.abs(dx) + Math.abs(dy) > 8) {
                            root.dragActive = true
                            var globalPos = dragMa.mapToGlobal(mouse.x, mouse.y)
                            root.dragStarted(root.instanceId)
                            root.dragMoved(root.instanceId, globalPos.x, globalPos.y)
                        }
                    } else {
                        var globalPos = dragMa.mapToGlobal(mouse.x, mouse.y)
                        root.dragMoved(root.instanceId, globalPos.x, globalPos.y)
                    }
                }

                onReleased: function(mouse) {
                    if (root.dragActive) {
                        root.dragActive = false
                        root.dragEnded(root.instanceId)
                    }
                }

                onCanceled: function(mouse) {
                    if (root.dragActive) {
                        root.dragActive = false
                        root.dragEnded(root.instanceId)
                    }
                }
            }
        }

        // --- 内容区 (纯容器, 子控件自管滚动) ---
        Item {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 4
            clip: true

            Loader {
                id: contentLoader
                anchors.fill: parent
                asynchronous: true

                source: {
                    var meta = WR.WidgetRegistry.getWidgetMeta(root.typeName)
                    return meta ? meta.source : ""
                }

                onLoaded: {
                    if (item) {
                        // 关键: 子控件填满内容区, 否则保持 implicitHeight 导致错位
                        item.width = Qt.binding(function() { return contentArea.width })
                        item.height = Qt.binding(function() { return contentArea.height })
                        if (item.hasOwnProperty("widgetConfig")) {
                            item.widgetConfig = Qt.binding(function() { return root.widgetConfig })
                        }
                    }
                }
            }
        }
    }

    // 整体 hover 检测
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        propagateComposedEvents: true
        onEntered: root.hovered = true
        onExited: root.hovered = false
        onClicked: function(mouse) { mouse.accepted = false }
        onPressed: function(mouse) { mouse.accepted = false }
        onReleased: function(mouse) { mouse.accepted = false }
    }

    // ============ 合并指示 ============
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 4
        color: "#3B82F6"
        visible: root.mergeHighlight === "left"
        z: 25; opacity: 0.9
    }
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 4
        color: "#3B82F6"
        visible: root.mergeHighlight === "right"
        z: 25; opacity: 0.9
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 4
        color: "#10B981"
        visible: root.mergeHighlight === "top"
        z: 25; opacity: 0.9
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 4
        color: "#10B981"
        visible: root.mergeHighlight === "bottom"
        z: 25; opacity: 0.9
    }

    // ============ 缩放手柄 ============

    // 右边缘 — 调列宽 (发送绝对值)
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 36
        width: 8
        color: "transparent"
        z: 20

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SplitHCursor
            property int origCS: 0
            property real pxPerUnit: 0
            onPressed: function(mouse) {
                origCS = root.gridColSpan
                pxPerUnit = Math.max(30, root.width / 12)
            }
            onPositionChanged: function(mouse) {
                var d = Math.round(mouse.x / pxPerUnit)
                var nv = Math.max(1, Math.min(12, origCS + d))
                if (nv !== root.gridColSpan)
                    root.resizeRequest(root.instanceId, nv, root.gridRowSpan)
            }
        }
    }

    // 下边缘 — 调行高 (发送绝对值)
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        height: 8
        color: "transparent"
        z: 20

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SplitVCursor
            property int origRS: 0
            property real pxPerUnit: 0
            onPressed: function(mouse) {
                origRS = root.gridRowSpan
                pxPerUnit = Math.max(40, root.height / 8)
            }
            onPositionChanged: function(mouse) {
                var d = Math.round(mouse.y / pxPerUnit)
                var nv = Math.max(1, origRS + d)
                if (nv !== root.gridRowSpan)
                    root.resizeRequest(root.instanceId, root.gridColSpan, nv)
            }
        }
    }

    // 右下角
    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 16
        height: 16
        color: root.hovered ? Qt.rgba(1,1,1,0.15) : "transparent"
        radius: 2
        z: 20

        Canvas {
            anchors.fill: parent
            visible: root.hovered
            onPaint: {
                var ctx = getContext("2d")
                ctx.strokeStyle = "#64748B"; ctx.lineWidth = 1.5
                ctx.beginPath(); ctx.moveTo(width-2,2); ctx.lineTo(2,height-2)
                ctx.moveTo(width-6,2); ctx.lineTo(2,height-6); ctx.stroke()
            }
        }

        MouseArea {
            anchors.fill: parent
            anchors.margins: -4
            cursorShape: Qt.SizeFDiagCursor
            property int origCS: 0
            property int origRS: 0
            property real pxPerUnitX: 0
            property real pxPerUnitY: 0
            onPressed: function(mouse) {
                origCS = root.gridColSpan; origRS = root.gridRowSpan
                pxPerUnitX = Math.max(30, root.width / 12)
                pxPerUnitY = Math.max(40, root.height / 8)
            }
            onPositionChanged: function(mouse) {
                var dc = Math.round(mouse.x / pxPerUnitX)
                var dr = Math.round(mouse.y / pxPerUnitY)
                var nc = Math.max(1, Math.min(12, origCS + dc))
                var nr = Math.max(1, origRS + dr)
                if (nc !== root.gridColSpan || nr !== root.gridRowSpan)
                    root.resizeRequest(root.instanceId, nc, nr)
            }
        }
    }
}
