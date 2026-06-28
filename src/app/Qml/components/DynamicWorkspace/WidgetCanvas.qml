import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root

    property var model: null
    property int gap: 8
    readonly property int gridUnits: 12
    readonly property int minRowHeight: 120

    signal widgetRemoveRequest(string instanceId)
    signal widgetConfigRequest(string instanceId)
    signal widgetReorderRequest(int fromIndex, int toIndex)
    signal widgetResizeRequest(string instanceId, int colSpan, int rowSpan)
    signal widgetMergeRequest(int fromIndex, int toIndex, string side)

    // ============ 拖拽 ============
    property string draggedId: ""
    property int draggedIdx: -1
    property int lastReorderTarget: -1
    property real dragGX: 0
    property real dragGY: 0
    property bool isDragging: false

    // ============ 合并指示 (top/bottom/left/right) ============
    property int reorderTargetIdx: -1
    property string mergeTargetId: ""
    property string mergeSide: ""

    property real contentHeight: 0

    function relayout() {
        if (!root.model || root.model.count === 0) { root.contentHeight = 0; return }
        var w = root.width, h = root.height
        if (w <= 0 || h <= 0) return

        // ---- 分行 ----
        var rows = []
        var cur = null
        for (var i = 0; i < root.model.count; i++) {
            var item = root.model.get(i)
            var cs = clamp(Math.max(1, item.colSpan || 1), 1, root.gridUnits)
            var rs = Math.max(1, item.rowSpan || 1)
            if (!cur) cur = { widgets: [], totalCS: 0, maxRS: 0 }
            if (cur.widgets.length > 0 && cur.totalCS + cs > root.gridUnits) {
                rows.push(cur); cur = { widgets: [], totalCS: 0, maxRS: 0 }
            }
            cur.widgets.push({ idx: i, cs: cs, rs: rs })
            cur.totalCS += cs
            cur.maxRS = Math.max(cur.maxRS, rs)
        }
        if (cur && cur.widgets.length > 0) rows.push(cur)

        // ---- 高度: 自然 vs 视口, 取大者 ----
        var totalRS = 0
        for (var ri = 0; ri < rows.length; ri++) totalRS += rows[ri].maxRS
        var gapH = Math.max(0, rows.length - 1) * root.gap
        var naturalH = root.minRowHeight * totalRS + gapH
        var usedH = Math.max(h, naturalH)

        var availH = usedH - gapH
        var y = 0

        for (ri = 0; ri < rows.length; ri++) {
            var row = rows[ri]
            var rowH = availH * row.maxRS / totalRS
            var gapW = Math.max(0, row.widgets.length - 1) * root.gap
            var availW = w - gapW
            var x = 0
            for (var wi = 0; wi < row.widgets.length; wi++) {
                var wd = row.widgets[wi]
                var c = widgetRepeater.itemAt(wd.idx)
                if (!c) continue
                c.x = x; c.y = y
                c.width = availW * wd.cs / row.totalCS
                c.height = rowH
                c.mergeHighlight = (c.instanceId === root.mergeTargetId) ? root.mergeSide : ""
                x += c.width + root.gap
            }
            y += rowH + root.gap
        }
        root.contentHeight = y - root.gap
    }

    function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v) }

    // ============ 拖拽 ============
    function onFrameDragStarted(id) {
        root.draggedId = id; root.isDragging = true
        root.lastReorderTarget = -1; root.mergeTargetId = ""; root.mergeSide = ""
        for (var i = 0; i < widgetRepeater.count; i++) {
            var it = widgetRepeater.itemAt(i)
            if (it && it.instanceId === id) { root.draggedIdx = i; root.lastReorderTarget = i; break }
        }
    }
    function onFrameDragMoved(id, gx, gy) {
        root.dragGX = gx; root.dragGY = gy
        detectMergeTarget(); detectReorderTarget()
    }
    function onFrameDragEnded(id) {
        // 松开鼠标后才执行合并或排序，避免拖拽期间闪烁
        if (root.mergeTargetId && root.mergeSide) {
            var ti = -1
            for (var i = 0; i < widgetRepeater.count; i++) {
                var it = widgetRepeater.itemAt(i)
                if (it && it.instanceId === root.mergeTargetId) { ti = i; break }
            }
            if (ti >= 0 && ti !== root.draggedIdx)
                root.widgetMergeRequest(root.draggedIdx, ti, root.mergeSide)
        } else if (root.reorderTargetIdx >= 0 && root.reorderTargetIdx !== root.draggedIdx) {
            root.widgetReorderRequest(root.draggedIdx, root.reorderTargetIdx)
        }
        root.isDragging = false; root.draggedId = ""
        root.draggedIdx = -1; root.lastReorderTarget = -1
        root.reorderTargetIdx = -1
        root.mergeTargetId = ""; root.mergeSide = ""
    }

    function detectMergeTarget() {
        if (!root.isDragging) return
        var lp = canvas.mapFromGlobal(root.dragGX, root.dragGY)
        var lx = lp.x, ly = lp.y
        root.mergeTargetId = ""; root.mergeSide = ""
        for (var i = 0; i < widgetRepeater.count; i++) {
            var c = widgetRepeater.itemAt(i)
            if (!c || i === root.draggedIdx) continue
            // 水平范围: 在控件宽度内 (或扩展到全宽用于上下插入)
            var inH = (lx >= c.x - 20 && lx <= c.x + c.width + 20)
            var inV = (ly >= c.y - 20 && ly <= c.y + c.height + 20)
            if (!inH || !inV) continue

            var relX = (lx - c.x) / c.width
            var relY = (ly - c.y) / c.height

            // 优先左右 (在垂直中间区域)
            if (relY >= 0.25 && relY <= 0.75) {
                if (relX < 0.3) { root.mergeTargetId = c.instanceId; root.mergeSide = "left"; return }
                if (relX > 0.7) { root.mergeTargetId = c.instanceId; root.mergeSide = "right"; return }
            }
            // 上下 (在水平中间区域)
            if (relX >= 0.25 && relX <= 0.75) {
                if (relY < 0.3) { root.mergeTargetId = c.instanceId; root.mergeSide = "top"; return }
                if (relY > 0.7) { root.mergeTargetId = c.instanceId; root.mergeSide = "bottom"; return }
            }
        }
    }

    function detectReorderTarget() {
        if (!root.isDragging || !root.model) return
        var ly = canvas.mapFromGlobal(0, root.dragGY).y
        var best = -1, bestD = Infinity
        for (var i = 0; i < widgetRepeater.count; i++) {
            var c = widgetRepeater.itemAt(i)
            if (!c || i === root.draggedIdx) continue
            var d = Math.abs(ly - (c.y + c.height/2))
            if (d < bestD) { bestD = d; best = i }
        }
        root.reorderTargetIdx = best
    }

    // ============ 主容器 ============
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: root.width
        contentHeight: root.contentHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Item {
            id: canvas
            width: root.width
            height: root.contentHeight

            Repeater {
                id: widgetRepeater
                model: root.model
                delegate: WidgetFrame {
                    instanceId: model.instanceId
                    typeName: model.typeName
                    title: model.title
                    widgetConfig: model.widgetConfig
                    gridColSpan: model.colSpan
                    gridRowSpan: model.rowSpan
                    onCloseRequest: root.widgetRemoveRequest(instanceId)
                    onConfigRequest: root.widgetConfigRequest(instanceId)
                    onDragStarted: root.onFrameDragStarted(instanceId)
                    onDragMoved: root.onFrameDragMoved(instanceId, globalX, globalY)
                    onDragEnded: root.onFrameDragEnded(instanceId)
                    onResizeRequest: root.widgetResizeRequest(instanceId, colSpan, rowSpan)
                }
            }
        }
    }

    function clearState() {
        root.isDragging = false; root.draggedId = ""; root.draggedIdx = -1
        root.lastReorderTarget = -1; root.reorderTargetIdx = -1
        root.mergeTargetId = ""; root.mergeSide = ""
    }

    onWidthChanged: Qt.callLater(root.relayout)
    onHeightChanged: Qt.callLater(root.relayout)
    Component.onCompleted: Qt.callLater(root.relayout)
}
