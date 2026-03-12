// MetaTable.qml - 通用表格组件
// 配置驱动，支持多种字段类型和操作
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.qmlmodels 1.15

/**
 * 通用表格组件
 * 完全由配置驱动，不包含任何业务逻辑
 * 支持：文本、数字、徽章、标签、进度条、日期等字段类型
 */
Rectangle {
    id: root
    
    // ============ 输入属性 ============
    
    // 数据模型
    property var model: null
    // 字段配置数组
    property var fields: []
    // 全局操作（工具栏按钮）
    property var actions: []
    // 行内操作（每行末尾的按钮）
    property var itemActions: []
    // 是否支持多选
    property bool selectable: false
    // 是否显示工具栏
    property bool showToolbar: true
    // 表格标题
    property string title: ""
    // 副标题
    property string subtitle: ""
    
    // ============ 输出信号 ============
    
    // 工具栏操作触发
    signal actionTriggered(string actionName, var context)
    // 行内操作触发
    signal itemActionTriggered(string actionName, var itemData, int rowIndex)
    // 行双击
    signal rowDoubleClicked(int rowIndex, var itemData)
    // 行单击
    signal rowClicked(int rowIndex, var itemData)
    // 选择变化
    signal selectionChanged(var selectedRows)
    
    // ============ 内部状态 ============
    
    // 当前选中行
    property var selectedRows: []
    // 搜索文本
    property string searchText: ""
    // 排序字段
    property string sortField: ""
    // 排序方向
    property bool sortAscending: true
    
    // 分页相关
    property int currentPage: 1
    property int pageSize: 20
    property int totalCount: 0
    property int totalPages: Math.ceil(totalCount / Math.max(pageSize, 1))
    
    // 加载状态
    property bool loading: false
    property string loadingText: "加载中..."
    
    // 空状态
    property bool showEmptyState: true
    property string emptyText: "暂无数据"
    property string emptyIcon: "📄"
    
    // 可见字段（过滤掉不可见字段）
    property var visibleFields: fields ? fields.filter(function(f) { 
        return f.visible !== false 
    }) : []
    
    // ============ 视觉样式 ============
    
    radius: 8  // borderRadiusLg
    color: "#FAFAFA"  // bgSecondary
    border.color: "#D9D9D9"  // borderDefault
    border.width: 1
    
    // ============ 主布局 ============
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16  // spacing4
        spacing: 16  // spacing4
        
        // 标题区域（如果有标题）
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: titleText.visible ? titleText.height + subtitleText.height + 8 : 0
            visible: title !== "" || subtitle !== ""
            
            ColumnLayout {
                anchors.fill: parent
                spacing: 4
                
                Text {
                    id: titleText
                    text: root.title
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                    color: "#262626"
                    visible: root.title !== ""
                }
                
                Text {
                    id: subtitleText
                    text: root.subtitle
                    font.pixelSize: 12
                    color: "#666666"
                    visible: root.subtitle !== ""
                }
            }
        }
        
        // 工具栏
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            visible: root.showToolbar && (actions.length > 0 || selectable || searchField.visible)
            
            // 批量选择提示
            Text {
                text: `已选择 ${selectedRows.length} 项`
                color: "#1890FF"
                font.pixelSize: 12
                visible: selectable && selectedRows.length > 0
            }
            
            // 搜索框
            Rectangle {
                id: searchField
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                radius: 6
                color: "#F5F5F5"
                border.width: 1
                border.color: "#D9D9D9"
                visible: true
                
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    spacing: 8
                    
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "🔍"
                        font.pixelSize: 14
                        color: "#999999"
                    }
                    
                    TextInput {
                        id: searchInput
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 50
                        font.pixelSize: 14
                        color: "#262626"
                        
                        onTextChanged: {
                            root.searchText = text
                        }
                        
                        Text {
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            text: "搜索..."
                            font: searchInput.font
                            color: "#999999"
                            visible: !searchInput.text && !searchInput.activeFocus
                        }
                    }
                }
            }
            
            // 工具栏按钮
            Row {
                spacing: 8
                visible: actions.length > 0
                
                Repeater {
                    model: actions
                    
                    delegate: Rectangle {
                        width: actionText.width + 16 * 2
                        height: 32
                        radius: 6
                        color: modelData.type === "primary" ? "#1890FF" : 
                               modelData.type === "danger" ? "#F5222D" + "20" : 
                               "transparent"
                        border.color: modelData.type === "default" ? "#D9D9D9" : "transparent"
                        border.width: 1
                        
                        Text {
                            id: actionText
                            anchors.centerIn: parent
                            text: modelData.label
                            font.pixelSize: 14
                            color: modelData.type === "primary" ? "white" : 
                                   modelData.type === "danger" ? "#F5222D" : 
                                   "#262626"
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            
                            onClicked: {
                                if (modelData.confirm) {
                                    // 需要确认的操作
                                    confirmDialog.show(
                                        modelData.confirmText || `确定执行${modelData.label}吗？`,
                                        function() {
                                            root.actionTriggered(modelData.name, {
                                                selectedRows: selectedRows,
                                                action: modelData
                                            })
                                        }
                                    )
                                } else {
                                    root.actionTriggered(modelData.name, {
                                        selectedRows: selectedRows,
                                        action: modelData
                                    })
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // 表头
        Row {
            spacing: 0
            Layout.fillWidth: true
            height: 40
            
            // 选择列
            Rectangle {
                width: selectable ? 40 : 0
                height: parent.height
                color: "#F5F5F5"
                visible: selectable
                
                CheckBox {
                    anchors.centerIn: parent
                    checkState: {
                        if (selectedRows.length === 0) {
                            return Qt.Unchecked
                        } else if (selectedRows.length === (model ? model.rowCount : 0)) {
                            return Qt.Checked
                        } else {
                            return Qt.PartiallyChecked
                        }
                    }
                    
                    onClicked: {
                        if (checkState === Qt.Checked) {
                            // 全不选
                            selectedRows = []
                        } else {
                            // 全选
                            var allRows = []
                            var count = model ? model.rowCount : 0
                            for (var i = 0; i < count; i++) {
                                allRows.push(i)
                            }
                            selectedRows = allRows
                        }
                        selectionChanged(selectedRows)
                    }
                }
            }
            
            // 数据列头
            Repeater {
                model: visibleFields
                
                delegate: Rectangle {
                    width: modelData.width || 150
                    height: parent.height
                    color: "#F5F5F5"
                    border.width: 1
                    border.color: "#D9D9D9"
                    
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 4
                        
                        Text {
                            text: modelData.label || modelData.name
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            color: "#262626"
                            elide: Text.ElideRight
                        }
                        
                        // 排序图标
                        Text {
                            text: {
                                if (!modelData.sortable) return ""
                                if (root.sortField === modelData.name) {
                                    return root.sortAscending ? "↑" : "↓"
                                }
                                return "⇅"
                            }
                            font.pixelSize: 12
                            color: root.sortField === modelData.name ? "#1890FF" : 
                                   "#999999"
                            visible: modelData.sortable
                        }
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: modelData.sortable ? Qt.PointingHandCursor : Qt.ArrowCursor
                        enabled: modelData.sortable
                        
                        onClicked: {
                            if (root.sortField === modelData.name) {
                                root.sortAscending = !root.sortAscending
                            } else {
                                root.sortField = modelData.name
                                root.sortAscending = true
                            }
                            // 触发排序信号
                            root.actionTriggered("sort", {
                                field: modelData.name,
                                ascending: root.sortAscending
                            })
                        }
                    }
                }
            }
            
            // 操作列头
            Rectangle {
                width: itemActions.length * 70
                height: parent.height
                color: "#F5F5F5"
                border.width: 1
                border.color: "#D9D9D9"
                visible: itemActions.length > 0
                
                Text {
                    anchors.centerIn: parent
                    text: "操作"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: "#262626"
                }
            }
        }
        
        // 表格内容区域
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            // 加载状态
            Rectangle {
                anchors.fill: parent
                color: "#FAFAFA"
                visible: root.loading
                z: 10
                
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 16
                    
                    Text {
                        text: root.loadingText
                        font.pixelSize: 14
                        color: "#666666"
                    }
                    
                    // 加载动画
                    Rectangle {
                        width: 40
                        height: 40
                        radius: 20
                        color: "transparent"
                        border.color: "#1890FF"
                        border.width: 2
                        
                        RotationAnimation on rotation {
                            from: 0
                            to: 360
                            duration: 1000
                            loops: Animation.Infinite
                            running: root.loading
                        }
                    }
                }
            }
            
            // 空状态
            Rectangle {
                anchors.fill: parent
                color: "#FAFAFA"
                visible: root.showEmptyState && (!model || model.count === 0) && !root.loading
                z: 5
                
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 16
                    
                    Text {
                        text: root.emptyIcon
                        font.pixelSize: 48
                        color: "#999999"
                    }
                    
                    Text {
                        text: root.emptyText
                        font.pixelSize: 16
                        color: "#666666"
                    }
                    
                    Text {
                        text: "尝试搜索其他关键词或创建新数据"
                        font.pixelSize: 12
                        color: "#999999"
                        visible: root.searchText !== ""
                    }
                }
            }
            
            // 表格内容
            ListView {
                id: listView
                anchors.fill: parent
                clip: true
                
                model: root.model
                delegate: tableRow
                
                visible: !root.loading && (model && model.count > 0)
                
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    width: 8
                }
            }
        }
        
        // 分页控件
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            visible: totalPages > 1
            
            Text {
                text: `共 ${totalCount} 条，第 ${currentPage}/${totalPages} 页`
                font.pixelSize: 12
                color: "#666666"
            }
            
            Item { Layout.fillWidth: true }
            
            Row {
                spacing: 4
                
                // 上一页按钮
                Rectangle {
                    width: 32
                    height: 32
                    radius: 6
                    color: currentPage === 1 ? "#F5F5F5" : 
                           "#F0F0F0"
                    border.width: 1
                    border.color: "#D9D9D9"
                    
                    Text {
                        anchors.centerIn: parent
                        text: "◀"
                        font.pixelSize: 12
                        color: currentPage === 1 ? "#999999" : 
                               "#262626"
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        enabled: currentPage > 1
                        
                        onClicked: {
                            currentPage--
                            root.actionTriggered("pageChanged", {page: currentPage})
                        }
                    }
                }
                
                // 页码按钮
                Repeater {
                    model: {
                        var pages = []
                        var start = Math.max(1, currentPage - 2)
                        var end = Math.min(totalPages, currentPage + 2)
                        for (var i = start; i <= end; i++) {
                            pages.push(i)
                        }
                        return pages
                    }
                    
                    delegate: Rectangle {
                        width: 32
                        height: 32
                        radius: 6
                        color: modelData === currentPage ? "#1890FF" : 
                               "#F0F0F0"
                        border.width: 1
                        border.color: "#D9D9D9"
                        
                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            font.pixelSize: 12
                            color: modelData === currentPage ? "white" : 
                                   "#262626"
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            
                            onClicked: {
                                if (modelData !== currentPage) {
                                    currentPage = modelData
                                    root.actionTriggered("pageChanged", {page: currentPage})
                                }
                            }
                        }
                    }
                }
                
                // 下一页按钮
                Rectangle {
                    width: 32
                    height: 32
                    radius: 6
                    color: currentPage === totalPages ? "#F5F5F5" : 
                           "#F0F0F0"
                    border.width: 1
                    border.color: "#D9D9D9"
                    
                    Text {
                        anchors.centerIn: parent
                        text: "▶"
                        font.pixelSize: 12
                        color: currentPage === totalPages ? "#999999" : 
                               "#262626"
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        enabled: currentPage < totalPages
                        
                        onClicked: {
                            currentPage++
                            root.actionTriggered("pageChanged", {page: currentPage})
                        }
                    }
                }
            }
        }
    }
    
    // ============ 表格行组件 ============
    
    Component {
        id: tableRow
        
        Item {
            id: rowItem
            width: listView.width
            height: 48
            
            property var rowData: model
            property int rowIndex: index
            
            Row {
                spacing: 0
                anchors.fill: parent
                
                // 选择框
                Rectangle {
                    width: selectable ? 40 : 0
                    height: parent.height
                    color: index % 2 === 0 ? "#FAFAFA" : 
                           "#F5F5F5"
                    visible: selectable
                    
                    CheckBox {
                        anchors.centerIn: parent
                        checked: selectedRows.includes(index)
                        
                        onClicked: {
                            var newSelection = selectedRows.slice()
                            if (checked) {
                                if (!newSelection.includes(index)) {
                                    newSelection.push(index)
                                }
                            } else {
                                var pos = newSelection.indexOf(index)
                                if (pos >= 0) {
                                    newSelection.splice(pos, 1)
                                }
                            }
                            selectedRows = newSelection
                            selectionChanged(selectedRows)
                        }
                    }
                }
                
                // 数据单元格
                Repeater {
                    model: visibleFields
                    
                    delegate: Rectangle {
                        width: modelData.width || 150
                        height: parent.height
                        color: index % 2 === 0 ? "#FAFAFA" : 
                               "#F5F5F5"
                        border.width: 1
                        border.color: "#D9D9D9"
                        
                        // 根据字段类型渲染内容
                        Loader {
                            anchors.fill: parent
                            anchors.margins: 8
                            
                            sourceComponent: getFieldRenderer(modelData.type)
                            
                            property var field: modelData
                            property var value: rowData[field.name]
                            property var rowData: rowItem.rowData
                        }
                    }
                }
                
                // 操作按钮列
                Rectangle {
                    width: itemActions.length * 70
                    height: parent.height
                    color: index % 2 === 0 ? "#FAFAFA" : 
                           "#F5F5F5"
                    border.width: 1
                    border.color: "#D9D9D9"
                    visible: itemActions.length > 0
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 8
                        
                        Repeater {
                            model: itemActions
                            
                            delegate: Rectangle {
                                width: actionText.width + 12 * 2
                                height: 28
                                radius: 6
                                color: modelData.type === "primary" ? "#1890FF" : 
                                       modelData.type === "danger" ? "#F5222D" + "20" : 
                                       "transparent"
                                border.color: modelData.type === "default" ? "#D9D9D9" : "transparent"
                                border.width: 1
                                
                                Text {
                                    id: actionText
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    font.pixelSize: 12
                                    color: modelData.type === "primary" ? "white" : 
                                           modelData.type === "danger" ? "#F5222D" : 
                                           "#262626"
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    
                                    onClicked: {
                                        if (modelData.confirm) {
                                            confirmDialog.show(
                                                modelData.confirmText || `确定${modelData.label}吗？`,
                                                function() {
                                                    root.itemActionTriggered(
                                                        modelData.name,
                                                        rowItem.rowData,
                                                        rowItem.rowIndex
                                                    )
                                                }
                                            )
                                        } else {
                                            root.itemActionTriggered(
                                                modelData.name,
                                                rowItem.rowData,
                                                rowItem.rowIndex
                                            )
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // 行点击
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                
                onClicked: {
                    root.rowClicked(rowIndex, rowData)
                }
                
                onDoubleClicked: {
                    root.rowDoubleClicked(rowIndex, rowData)
                }
            }
        }
    }
    
    // ============ 字段渲染器组件 ============
    
    // 文本渲染
    Component {
        id: textRenderer
        
        Text {
            text: value || ""
            color: "#262626"
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }
    
    // 数字渲染
    Component {
        id: numberRenderer
        
        Text {
            text: {
                if (value === undefined || value === null) return ""
                if (field.format) {
                    return field.format.arg(value)
                }
                if (field.unit) {
                    return value.toFixed(field.precision || 2) + field.unit
                }
                return value.toFixed(field.precision || 2)
            }
            color: field.valueMap && field.valueMap[value] ? 
                   field.valueMap[value].color : "#262626"
            font.bold: field.valueMap && field.valueMap[value]
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }
    
    // 徽章渲染
    Component {
        id: badgeRenderer
        
        Rectangle {
            height: 24
            width: Math.min(badgeText.width + 16, parent.width)
            radius: 12
            color: {
                if (field.valueMap && field.valueMap[value]) {
                    return field.valueMap[value].color + "20"
                }
                return "#F0F0F0"
            }
            
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: field.valueMap && field.valueMap[value] ? 
                      field.valueMap[value].label : (value || "")
                color: field.valueMap && field.valueMap[value] ? 
                       field.valueMap[value].color : "#666666"
                font.pixelSize: 12
            }
        }
    }
    
    // 标签渲染
    Component {
        id: tagsRenderer
        
        Flow {
            spacing: 4
            
            Repeater {
                model: Array.isArray(value) ? value : []
                
                delegate: Rectangle {
                    height: 20
                    width: tagText.width + 12
                    radius: 10
                    color: "#F0F0F0"
                    
                    Text {
                        id: tagText
                        anchors.centerIn: parent
                        text: modelData
                        font.pixelSize: 10
                        color: "#666666"
                    }
                }
            }
        }
    }
    
    // 进度条渲染
    Component {
        id: progressRenderer
        
        Rectangle {
            height: 6
            width: parent.width
            radius: 3
            color: "#F0F0F0"
            
            Rectangle {
                width: parent.width * Math.min(Math.max(value || 0, 0), 1)
                height: parent.height
                radius: parent.radius
                color: "#1890FF"
            }
        }
    }
    
    // 布尔值渲染
    Component {
        id: booleanRenderer
        
        Text {
            text: value ? "✓" : "✗"
            color: value ? "#52C41A" : "#F5222D"
            font.pixelSize: 14
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            anchors.centerIn: parent
        }
    }
    
    // 日期渲染
    Component {
        id: dateRenderer
        
        Text {
            text: {
                if (!value) return ""
                var date = new Date(value)
                if (field.format === "date") {
                    return date.toLocaleDateString(Qt.locale(), "yyyy-MM-dd")
                } else if (field.format === "datetime") {
                    return date.toLocaleString(Qt.locale(), "yyyy-MM-dd hh:mm")
                }
                return date.toLocaleDateString(Qt.locale(), "yyyy-MM-dd")
            }
            color: "#262626"
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }
    
    // 获取字段渲染器
    function getFieldRenderer(type) {
        switch(type) {
            case "text": return textRenderer
            case "number": return numberRenderer
            case "badge": return badgeRenderer
            case "tags": return tagsRenderer
            case "progress": return progressRenderer
            case "boolean": return booleanRenderer
            case "date": return dateRenderer
            case "datetime": return dateRenderer
            default: return textRenderer
        }
    }
    
    // ============ 对话框组件 ============
    
    // 确认对话框
    Popup {
        id: confirmDialog
        width: 300
        height: 180
        modal: true
        dim: true
        
        property var callback: null
        property string message: ""
        
        function show(text, cb) {
            message = text
            callback = cb
            open()
        }
        
        background: Rectangle {
            color: "#FAFAFA"
            radius: 8
            border.color: "#D9D9D9"
            border.width: 1
        }
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            
            Text {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: confirmDialog.message
                color: "#262626"
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            
            RowLayout {
                Layout.fillWidth: true
                
                Button {
                    Layout.fillWidth: true
                    text: "取消"
                    onClicked: confirmDialog.close()
                }
                
                Button {
                    Layout.fillWidth: true
                    text: "确定"
                    highlighted: true
                    onClicked: {
                        if (confirmDialog.callback) {
                            confirmDialog.callback()
                        }
                        confirmDialog.close()
                    }
                }
            }
        }
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        // 初始化选中行数组
        selectedRows = []
    }
}