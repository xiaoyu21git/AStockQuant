// FactorSelectorDialog.qml
// 因子选择对话框 - 简化版本
// 设计原则：
// 1. QML只负责显示，不操作数据
// 2. 所有数据操作通过C++控制器完成
// 3. 使用属性绑定更新UI状态

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge

Dialog {
    id: root
    
    // 属性
    property Bridge.FactorBacktestController factorBacktestController: null
    property Bridge.FactorService factorService: null
    property Bridge.FactorViewModel factorViewModel: null
    property var selectedFactorIds: []
    property string dataSourceMode: "cache"
    property bool supportMapRequested: false
    property bool supportMapLoading: false
    property var factorSupportMap: ({})
    property var supportMapRefreshCallback: null
    
    // 内部属性 - 用于UI显示
    property var uiSelectedFactorIds: []

    function normalizedFactorIdKey(factorId) {
        return String(factorId === undefined || factorId === null ? "" : factorId)
    }

    function supportInfoForFactor(factorId) {
        var factorKey = normalizedFactorIdKey(factorId)
        if (factorSupportMap && factorSupportMap[factorKey] !== undefined) {
            return factorSupportMap[factorKey]
        }

        if (supportMapLoading) {
            return {
                supported: false,
                requiredFields: [],
                missingFields: [],
                reason: "支持图加载中"
            }
        }

        return {
            supported: false,
            requiredFields: [],
            missingFields: [],
            reason: supportMapRequested ? "校验结果未返回" : "请先点击开始校验"
        }
    }

    function isFactorSupported(factorId) {
        return supportInfoForFactor(factorId).supported !== false
    }

    function factorUiMetaFor(typeValue) {
        var numericType = Number(typeValue)
        if (isNaN(numericType) || numericType < 0) {
            return ({})
        }

        return Bridge.FactorMetaService.getFactorUiMeta(numericType) || ({})
    }

    function factorCategoryColor(typeValue) {
        var meta = factorUiMetaFor(typeValue)
        if (meta.color !== undefined && meta.color !== null) {
            return Qt.color(String(meta.color))
        }

        return Qt.color("#94A3B8")
    }

    function factorCategoryLabel(typeValue) {
        var meta = factorUiMetaFor(typeValue)
        return meta.displayName !== undefined && meta.displayName !== null ? String(meta.displayName) : ""
    }

    function supportedFactorCount() {
        if (!factorViewModel) {
            return 0
        }

        var count = 0
        for (var i = 0; i < factorViewModel.rowCount(); i++) {
            var factorId = factorViewModel.data(factorViewModel.index(i, 0), 257)
            if (factorId && isFactorSupported(factorId)) {
                count++
            }
        }
        return count
    }

    function sanitizeSelectedFactors() {
        if (!supportMapRequested || supportMapLoading) {
            isAllSelected = false
            return
        }

        if (!factorViewModel) {
            isAllSelected = false
            return
        }

        var selectedSupportedCount = 0
        for (var i = 0; i < selectedFactorIds.length; i++) {
            if (isFactorSupported(selectedFactorIds[i])) {
                selectedSupportedCount++
            }
        }

        isAllSelected = supportedFactorCount() > 0 && selectedSupportedCount === supportedFactorCount()
    }
    
    // 信号
    signal factorsSelected(var factorIds)
    signal dialogClosed()
    
    // 对话框设置
    modal: true
    closePolicy: Popup.NoAutoClose
    width: 850
    height: 650
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    padding: 0
    
    // 背景
    background: Rectangle {
        radius: 12
        color: "#121828"
        border.width: 1
        border.color: "#2d3748"
    }
    
    // 主布局
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // 标题栏
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: "#1a2235"
            radius: 12
            border.width: 0
            
            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                
                Text {
                    text: "📊 选择因子"
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: "#f1f5f9"
                }
                
                Item { Layout.fillWidth: true }
                
                // 关闭按钮
                Rectangle {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    radius: 6
                    color: "transparent"
                    
                    Text {
                        anchors.centerIn: parent
                        text: "×"
                        font.pixelSize: 20
                        color: "#94a3b8"
                    }
                    
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                // 关闭对话框
                                root.close()
                                dialogClosed()
                            }
                        }
                }
            }
        }
        
        // 搜索和操作区域
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 170
            color: "#121828"
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    radius: 8
                    color: supportMapRequested ? "#1d4ed81a" : "#0f172a"
                    border.width: 1
                    border.color: supportMapRequested ? "#3b82f6" : "#475569"
                    visible: true

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 10

                        BusyIndicator {
                            Layout.alignment: Qt.AlignVCenter
                            running: supportMapLoading
                            visible: supportMapLoading
                            width: 20
                            height: 20
                        }

                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            text: supportMapLoading
                                  ? "正在校验可用因子，校验完成后列表会自动刷新"
                                  : (supportMapRequested
                                     ? "校验已完成，可继续选择因子"
                                     : "点击开始校验，再选择因子")
                            font.pixelSize: 13
                            color: supportMapLoading ? "#93c5fd" : (supportMapRequested ? "#86efac" : "#cbd5e1")
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 120
                            Layout.preferredHeight: 30
                            radius: 8
                            color: supportMapLoading ? "#475569" : "#2563eb"
                            border.width: 1
                            border.color: supportMapLoading ? "#64748b" : "#60a5fa"
                            opacity: supportMapLoading ? 0.8 : 1.0

                            Text {
                                anchors.centerIn: parent
                                text: supportMapLoading ? "校验中" : "开始校验"
                                font.pixelSize: 13
                                color: "white"
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                enabled: !supportMapLoading
                                onClicked: {
                                    console.log("点击开始校验按钮")
                                    startSupportMapRefresh()
                                }
                            }
                        }
                    }
                }
                
                // 搜索框
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 8
                    color: "#1a2235"
                    border.width: 1
                    border.color: "#475569"
                    
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 10
                        
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "🔍"
                            font.pixelSize: 16
                            color: "#94a3b8"
                        }
                        
                        TextInput {
                            id: searchInput
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 100
                            font.pixelSize: 14
                            color: "#f1f5f9"
                            
                            onTextChanged: {
                                // 搜索功能由C++控制器处理
                                console.log("搜索文本:", text)
                            }
                            
                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 2
                                verticalAlignment: Text.AlignVCenter
                                text: "搜索因子..."
                                font: parent.font
                                color: "#64748b"
                                visible: !parent.text && !parent.activeFocus
                            }
                        }
                    }
                }
                
                // 操作按钮行
                RowLayout {
                    spacing: 12
                    
                    // 全选复选框
                    Rectangle {
                        Layout.preferredWidth: 100
                        Layout.preferredHeight: 32
                        radius: 6
                        color: isAllSelected ? "#3b82f6" : "#1a2235"
                        border.width: 1
                        border.color: isAllSelected ? "#3b82f6" : "#475569"
                        
                        Row {
                            anchors.centerIn: parent
                            spacing: 6
                            
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: isAllSelected ? "✓" : "☐"
                                font.pixelSize: 14
                                color: isAllSelected ? "white" : "#94a3b8"
                            }
                            
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: isAllSelected ? "取消全选" : "全选"
                                font.pixelSize: 13
                                color: isAllSelected ? "white" : "#f1f5f9"
                            }
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            enabled: !supportMapLoading && supportMapRequested
                            onClicked: {
                                // 全选/取消全选逻辑
                                console.log("点击全选按钮, 当前状态:", isAllSelected)
                                
                                if (isAllSelected) {
                                    // 取消全选 - 清空选择列表
                                    selectedFactorIds = []
                                } else {
                                    // 全选 - 从 factorViewModel 获取所有因子ID
                                    var allIds = []
                                    if (factorViewModel) {
                                        for (var i = 0; i < factorViewModel.rowCount(); i++) {
                                            var factorId = factorViewModel.data(factorViewModel.index(i, 0), 257) // 257 = FactorIdRole
                                            if (factorId && isFactorSupported(factorId)) {
                                                allIds.push(normalizedFactorIdKey(factorId))
                                            }
                                        }
                                    }
                                    selectedFactorIds = allIds
                                }
                                
                                // 更新全选状态
                                isAllSelected = !isAllSelected
                            }
                        }
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    // 状态信息
                    Text {
                        text: `已选择: ${selectedFactorIds.length} 个`
                        font.pixelSize: 13
                        color: selectedFactorIds.length > 0 ? "#3b82f6" : "#94a3b8"
                        font.weight: Font.Medium
                    }
                    
                    Text {
                        text: supportMapLoading
                            ? " | 校验中..."
                            : (dataSourceMode === "cache"
                               ? ` | 可选: ${supportedFactorCount()} / ${factorViewModel ? factorViewModel.count : 0} 个`
                               : ` | 总计: ${factorViewModel ? factorViewModel.count : 0} 个`)
                        font.pixelSize: 13
                        color: "#64748b"
                    }
                    
                    // 清空按钮
                    Rectangle {
                        Layout.preferredWidth: 80
                        Layout.preferredHeight: 32
                        radius: 6
                        color: selectedFactorIds.length > 0 ? "#ef4444" : "#1a2235"
                        border.width: 1
                        border.color: selectedFactorIds.length > 0 ? "#ef4444" : "#475569"
                        
                        Text {
                            anchors.centerIn: parent
                            text: "清空"
                            font.pixelSize: 13
                            color: selectedFactorIds.length > 0 ? "white" : "#94a3b8"
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                // 清空选择逻辑
                                console.log("点击清空按钮")
                                selectedFactorIds = []
                                isAllSelected = false
                            }
                        }
                    }
                }
            }
        }
        
        // 因子列表区域
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#121828"
            
            ScrollView {
                anchors.fill: parent
                anchors.margins: 16
                
                ListView {
                    id: factorListView
                    width: parent.width
                    model: factorViewModel
                    spacing: 8
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    
                    delegate: Rectangle {
                        id: listItem
                        width: factorListView.width
                        height: 60
                        radius: 8
                        color: !supported ? "#151b2b" : (selected ? "#3b82f620" : (mouseArea.containsMouse ? "#1a2235" : "#222c44"))
                        border.width: 1
                        border.color: !supported ? "#3f4c63" : (selected ? "#3b82f6" : "#2d3748")
                        opacity: supported ? 1.0 : 0.55
                        
                        property bool selected: selectedFactorIds.includes(normalizedFactorIdKey(model.factorId))
                        property var supportInfo: root.supportInfoForFactor(model.factorId)
                        property bool supported: supportInfo.supported !== false
                        
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12
                            
                            // 复选框
                            Rectangle {
                                width: 20
                                height: 20
                                radius: 4
                                border.width: 1
                                border.color: !supported ? "#475569" : (selected ? "#3b82f6" : "#475569")
                                color: !supported ? "transparent" : (selected ? "#3b82f6" : "transparent")
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: "✓"
                                    color: "white"
                                    visible: selected
                                    font.pixelSize: 10
                                }
                            }
                            
                            // 因子信息
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                
                                Text {
                                    text: model.displayName || model.factorName
                                    font.pixelSize: 14
                                    color: !supported ? "#94a3b8" : (selected ? "#3b82f6" : "#f1f5f9")
                                    elide: Text.ElideRight
                                }
                                
                                Text {
                                    text: supported
                                          ? (model.description || "")
                                          : (supportInfo.reason || "当前缓存不支持该因子")
                                    font.pixelSize: 11
                                    color: supported ? "#94a3b8" : "#f59e0b"
                                    elide: Text.ElideRight
                                }
                            }
                            
                            // 类别标签
                            Rectangle {
                                id: categoryTag
                                Layout.alignment: Qt.AlignRight
                                width: 80
                                height: 24
                                radius: 12
                                property color categoryColor: root.factorCategoryColor(model.factorType)
                                
                                color: categoryColor + "20"
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: root.factorCategoryLabel(model.factorType)
                                    font.pixelSize: 10
                                    color: categoryTag.categoryColor
                                }
                            }
                        }
                        
                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: supportMapRequested && !supportMapLoading && supported
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
                            onClicked: {
                                if (!supported) {
                                    console.log("当前因子未通过校验，禁止选择:", model.factorId, supportInfo.reason)
                                    return
                                }

                                // 因子选择逻辑
                                console.log("点击因子:", model.factorId)
                                
                                // 切换选择状态
                                var currentIds = selectedFactorIds.slice() // 复制数组
                                var normalizedId = normalizedFactorIdKey(model.factorId)
                                var index = currentIds.indexOf(normalizedId)
                                if (index === -1) {
                                    // 添加到选择列表
                                    currentIds.push(normalizedId)
                                } else {
                                    // 从选择列表中移除
                                    currentIds.splice(index, 1)
                                }
                                
                                // 更新选择列表（触发属性变更通知）
                                selectedFactorIds = currentIds
                                
                                // 更新全选状态
                                if (factorViewModel) {
                                    isAllSelected = (supportedFactorCount() > 0 && selectedFactorIds.length === supportedFactorCount())
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // 操作按钮区域
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            color: "#1a2235"
            
            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                
                Item { Layout.fillWidth: true }
                
                // 取消按钮
                Rectangle {
                    Layout.preferredWidth: 100
                    Layout.preferredHeight: 40
                    radius: 8
                    color: "#1a2235"
                    border.width: 1
                    border.color: "#475569"
                    
                    Text {
                        anchors.centerIn: parent
                        text: "取消"
                        font.pixelSize: 14
                        color: "#f1f5f9"
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.close()
                            dialogClosed()
                        }
                    }
                }
                
                // 确定按钮
                Rectangle {
                    Layout.preferredWidth: 100
                    Layout.preferredHeight: 40
                    radius: 8
                    color: selectedFactorIds.length > 0 ? "#3b82f6" : "#475569"
                    
                    Text {
                        anchors.centerIn: parent
                        text: "确定"
                        font.pixelSize: 14
                        color: selectedFactorIds.length > 0 ? "white" : "#64748b"
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        enabled: selectedFactorIds.length > 0
                        onClicked: {
                            // 发射信号通知页面
                            factorsSelected(selectedFactorIds)
                            root.close()
                        }
                    }
                }
            }
        }
    }
    
    // 内部状态
    property bool isAllSelected: false
    property string searchText: ""
    
    // 对话框事件
    onOpened: {
        searchInput.text = ""
        searchText = ""
        sanitizeSelectedFactors()
        console.log("因子选择对话框打开")
    }

    onFactorSupportMapChanged: {
        sanitizeSelectedFactors()
    }

    onSelectedFactorIdsChanged: {
        sanitizeSelectedFactors()
    }
    
    onClosed: dialogClosed()
    
    // 初始化
    Component.onCompleted: {
        console.log("FactorSelectorDialog: 组件初始化完成")
    }

    function startSupportMapRefresh() {
        if (supportMapLoading) {
            return
        }

        console.log("开始执行因子校验")
        supportMapRequested = true
        supportMapLoading = true
        sanitizeSelectedFactors()
        if (supportMapRefreshCallback) {
            supportMapRefreshCallback()
        }
    }
}