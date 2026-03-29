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
    property int selectedDatasetId: -1
    property var cacheAvailableFields: []
    property var factorSupportMap: ({})
    
    // 内部属性 - 用于UI显示
    property var uiSelectedFactorIds: []

    function supportInfoForFactor(factorId) {
        var factorKey = String(factorId)
        if (factorSupportMap && factorSupportMap[factorKey] !== undefined) {
            return factorSupportMap[factorKey]
        }

        return {
            supported: true,
            requiredFields: [],
            missingFields: [],
            reason: ""
        }
    }

    function isFactorSupported(factorId) {
        return supportInfoForFactor(factorId).supported !== false
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
        var filteredFactorIds = []
        for (var i = 0; i < selectedFactorIds.length; i++) {
            if (isFactorSupported(selectedFactorIds[i])) {
                filteredFactorIds.push(selectedFactorIds[i])
            }
        }

        if (filteredFactorIds.length !== selectedFactorIds.length) {
            selectedFactorIds = filteredFactorIds
        }

        isAllSelected = factorViewModel
            ? (supportedFactorCount() > 0 && selectedFactorIds.length === supportedFactorCount())
            : false
    }
    
    // 信号
    signal factorsSelected(var factorIds)
    signal dialogClosed()
    
    // 对话框设置
    modal: true
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
            Layout.preferredHeight: 90
            color: "#121828"
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                
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
                                                allIds.push(factorId)
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
                        text: dataSourceMode === "cache"
                              ? ` | 可选: ${supportedFactorCount()} / ${factorViewModel ? factorViewModel.count : 0} 个`
                              : ` | 总计: ${factorViewModel ? factorViewModel.count : 0} 个`
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
                            enabled: selectedFactorIds.length > 0
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
                        
                        property bool selected: selectedFactorIds.includes(model.factorId)
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
                                
                                property color categoryColor: {
                                    switch(model.majorCategory) {
                                        case "技术指标": return "#3b82f6"
                                        case "基本面": return "#10b981"
                                        case "量化因子": return "#f59e0b"
                                        case "市场情绪": return "#8b5cf6"
                                        default: return "#94a3b8"
                                    }
                                }
                                
                                color: categoryColor + "20"
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: model.majorCategory || "其他"
                                    font.pixelSize: 10
                                    color: categoryTag.categoryColor
                                }
                            }
                        }
                        
                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: supported ? Qt.PointingHandCursor : Qt.ForbiddenCursor
                            onClicked: {
                                if (!supported) {
                                    console.log("当前缓存不支持因子:", model.factorId, supportInfo.reason)
                                    return
                                }

                                // 因子选择逻辑
                                console.log("点击因子:", model.factorId)
                                
                                // 切换选择状态
                                var currentIds = selectedFactorIds.slice() // 复制数组
                                var index = currentIds.indexOf(model.factorId)
                                if (index === -1) {
                                    // 添加到选择列表
                                    currentIds.push(model.factorId)
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
                            // 将选择的因子ID传递给控制器
                            if (factorBacktestController) {
                                factorBacktestController.setSelectedFactorIds(selectedFactorIds)
                            }
                            
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
    
    onClosed: dialogClosed()
    
    // 初始化
    Component.onCompleted: {
        console.log("FactorSelectorDialog: 组件初始化完成")
    }
}