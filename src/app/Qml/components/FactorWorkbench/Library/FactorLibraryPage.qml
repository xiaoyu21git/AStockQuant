// FactorLibraryPage.qml
// 因子库模块 - FactorWorkbench的因子库浏览页面
// 已更新为使用统一量化卡片组件库
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs 
import AStock.Bridge 1.0 
import ConsoleUi 1.0
import "../../../utils/FactorDataAdapter.js" as FactorAdapter
import "../../Factor" as FactorComponents
import "../../Base" as BaseComponents

/**
 * 因子库模块组件
 * 提供因子浏览、筛选、搜索和选择功能
 */
Item {
    id: root
    
    // ============ 信号 ============
    
    signal factorSelected(string factorId)
    signal factorDoubleClicked(string factorId)
    signal favoriteToggled(string factorId, bool favorite)
    signal previewRequested(string factorId)
    signal analyzeRequested(string factorId)
    signal addToPortfolio(string factorId)
    signal editRequested(string factorId)
    signal deleteRequested(string factorId)
    signal createRequested()
    signal deleteCompleted(string factorId, bool success, string message)
    
    // ============ 属性 ============
    property string viewMode: "grid"  // grid, list, compare
    
    // 筛选状态
    property var currentFilter: ({
        searchText: "",
        category: "all",
        sortBy: "ic",
        sortOrder: "desc",
        status: "active",
        minIC: 0.03,
        maxTurnover: 50
    })
    
    property var selectedFactors: []
    property string selectedFactorId: ""
    
    // ============ C++ 数据绑定 ============
    
    // 因子服务（从FactorWorkbench传递过来）
    property var factorService: null
    readonly property bool factorMutationInProgress: !!(factorService && factorService.mutationInProgress)
    
    // 因子视图模型（从FactorWorkbench传递过来）
    property var factorModel: null
    
    // 检查factorModel是否有效
    function checkFactorModel() {
        if (!factorModel) {
            console.error("❌ FactorLibraryPage: factorModel未设置")
            return false
        }
        console.log("✅ FactorLibraryPage: factorModel已设置，count:", factorModel.count)
        return true
    }

    // ============ UI ============
    
    Rectangle {
        anchors.fill: parent
        color: "#0F172A"
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            
            // 智能筛选栏
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 140
                color: "#0F172A"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10
                    
                    // 搜索栏
                    RowLayout {
                        spacing: 12
                        
                        // 搜索框
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            radius: 8
                            color: "#334155"
                            
                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                anchors.rightMargin: 16
                                spacing: 10
                                
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "🔍"
                                    font.pixelSize: 16
                                    color: "#94A3B8"
                                }
                                
                                TextInput {
                                    id: librarySearchInput
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 100
                                    font.pixelSize: 15
                                    color: "#F1F5F9"
                                    
                                    onTextChanged: currentFilter.searchText = text
                                    
                                    Text {
                                        anchors.fill: parent
                                        anchors.leftMargin: 2
                                        verticalAlignment: Text.AlignVCenter
                                        text: "搜索因子名称、描述或标签..."
                                        font: parent.font
                                        color: "#94A3B8"
                                        visible: !parent.text && !parent.activeFocus
                                    }
                                }
                            }
                        }
                        
                        // 新建因子按钮
                        Rectangle {
                            Layout.preferredWidth: 120
                            Layout.preferredHeight: 40
                            radius: 8
                            color: root.factorMutationInProgress ? "#475569" : "#10B981"
                            opacity: root.factorMutationInProgress ? 0.65 : 1
                            
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                
                                Text {
                                    text: "➕"
                                    font.pixelSize: 16
                                    color: "white"
                                }
                                
                                Text {
                                    text: "新建因子"
                                    font.pixelSize: 14
                                    color: "white"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: root.factorMutationInProgress ? Qt.ForbiddenCursor : Qt.PointingHandCursor
                                enabled: !root.factorMutationInProgress
                                onClicked: root.createRequested()
                            }
                        }
                        
                        // 自然语言搜索
                        Rectangle {
                            Layout.preferredWidth: 120
                            Layout.preferredHeight: 40
                            radius: 8
                            color: "#3B82F6"
                            
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                
                                Text {
                                    text: "🤖"
                                    font.pixelSize: 16
                                    color: "white"
                                }
                                
                                Text {
                                    text: "智能搜索"
                                    font.pixelSize: 14
                                    color: "white"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: startNaturalLanguageSearch()
                            }
                        }
                    }
                    
                    // 筛选条件行
                    Row {
                        spacing: 12
                        
                        // 类型筛选
                        Rectangle {
                            width: 100
                            height: 32
                            radius: 6
                            color: "#334155"
                            
                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                spacing: 6
                                
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "📊"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }
                                
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "全部 ▼"
                                    font.pixelSize: 13
                                    color: "#F1F5F9"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: categoryMenu.open()
                            }
                        }
                        
                        // IC筛选
                        Rectangle {
                            width: 100
                            height: 32
                            radius: 6
                            color: "#334155"
                            
                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                spacing: 6
                                
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "📈"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }
                                
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "IC > 0.03"
                                    font.pixelSize: 13
                                    color: "#F1F5F9"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: openICFilterDialog()
                            }
                        }
                        
                        // 换手率筛选
                        Rectangle {
                            width: 120
                            height: 32
                            radius: 6
                            color: "#334155"
                            
                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                spacing: 6
                                
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "🔄"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }
                                
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "换手率 < 50%"
                                    font.pixelSize: 13
                                    color: "#F1F5F9"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: openTurnoverFilterDialog()
                            }
                        }
                        
                        // 添加筛选条件
                        Rectangle {
                            width: 32
                            height: 32
                            radius: 6
                            color: "#3B82F6"
                            
                            Text {
                                anchors.centerIn: parent
                                text: "+"
                                font.pixelSize: 16
                                color: "white"
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: addFilterCondition()
                            }
                        }
                    }
                }
            }
            
            // 视图切换和因子显示区域
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#0F172A"
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16
                    
                    // 视图切换
                    RowLayout {
                        spacing: 8
                        
                        Text {
                            text: "视图切换:"
                            font.pixelSize: 14
                            color: "#94A3B8"
                        }
                        
                        // 卡片视图
                        Rectangle {
                            width: 100
                            height: 32
                            radius: 6
                            color: viewMode === "grid" ? "#3B82F6" : "#334155"
                            
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                
                                Text {
                                    text: "◼◼"
                                    font.pixelSize: 14
                                    color: viewMode === "grid" ? "white" : "#94A3B8"
                                }
                                
                                Text {
                                    text: "卡片视图"
                                    font.pixelSize: 14
                                    color: viewMode === "grid" ? "white" : "#94A3B8"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: viewMode = "grid"
                            }
                        }
                        
                        // 列表视图
                        Rectangle {
                            width: 100
                            height: 32
                            radius: 6
                            color: viewMode === "list" ? "#3B82F6" : "#334155"
                            
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                
                                Text {
                                    text: "≡"
                                    font.pixelSize: 14
                                    color: viewMode === "list" ? "white" : "#94A3B8"
                                }
                                
                                Text {
                                    text: "列表视图"
                                    font.pixelSize: 14
                                    color: viewMode === "list" ? "white" : "#94A3B8"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: viewMode = "list"
                            }
                        }
                        
                        // 对比视图
                        Rectangle {
                            width: 100
                            height: 32
                            radius: 6
                            color: viewMode === "compare" ? "#3B82F6" : "#334155"
                            
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                
                                Text {
                                    text: "⇄"
                                    font.pixelSize: 14
                                    color: viewMode === "compare" ? "white" : "#94A3B8"
                                }
                                
                                Text {
                                    text: "对比视图"
                                    font.pixelSize: 14
                                    color: viewMode === "compare" ? "white" : "#94A3B8"
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: viewMode = "compare"
                            }
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        // 已选择提示
                        Text {
                            text: selectedFactors.length > 0 ? `已选择 ${selectedFactors.length} 个因子` : ""
                            font.pixelSize: 14
                            color: "#3B82F6"
                            visible: selectedFactors.length > 0
                        }
                    }
                    
                    // 因子卡片/列表区域
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        
                        // 卡片视图
                        GridView {
                            id: gridView
                            anchors.fill: parent
                            cellWidth: Math.floor(parent.width / 3) - 16  // 每行3个，减去边距，确保正确对齐
                            cellHeight: 280  // 匹配卡片高度(260) + 边距(20)
                            clip: true
                            visible: viewMode === "grid"
                            
                            model: factorModel
                            
                            delegate: FactorCard {
                                width: gridView.cellWidth - 12  // 统一使用-12边距，确保对齐
                                height: gridView.cellHeight - 20  // 统一使用-20边距，与策略库保持一致
                                
                                // 基础属性映射
                                factorId: model.factorId || ""
                                factorName: model.factorName || model.name || "未命名因子"
                                // displayName: 移除，FactorCard使用factorName作为显示名称
                                majorCategory: model.majorCategory || "动量类"
                                subCategory: model.subCategory || "趋势动量"
                                description: model.description || "暂无描述"
                                icValue: model.icValue || model.ic || 0.0
                                irValue: model.irValue || model.ir || 0.0
                                validityDays: model.validityDays || 20
                                turnoverRate: model.turnoverRate !== undefined && model.turnoverRate !== null ? model.turnoverRate : 32
                                isRecommended: model.isRecommended || false
                                isFavorite: model.isFavorite || false
                                status: model.status || "ACTIVE"
                                tags: model.tags || []
                                creator: model.creator || "系统"
                                createDate: model.createDate || ""
                                // 使用数据适配器获取正确的颜色
                                categoryColor: FactorAdapter.getFactorCategoryColor(model.majorCategory || "动量类")
                                
                                // 布局和交互设置
                                selected: root.selectedFactorId === model.factorId
                                showMiniChart: true
                                showGroupReturns: false
                                groupReturns: model.groupReturns || []
                                
                                // 卡片尺寸设置（覆盖BaseQuantCard默认值）
                                cardWidth: gridView.cellWidth - 12
                                cardHeight: 260
                                
                                // 确保颜色正确应用
                                Component.onCompleted: {
                                    // 强制颜色更新
                                    var category = model.majorCategory || "动量类"
                                    categoryColor = FactorAdapter.getFactorCategoryColor(category)
                                }
                                
                                // 信号连接 - 只发送信号，不设置选中状态
                                onClicked: {
                                    root.factorSelected(model.factorId)
                                }
                                onDoubleClicked: {
                                    root.factorDoubleClicked(model.factorId)
                                }
                                onFavoriteToggled: {
                                    root.favoriteToggled(model.factorId, favorite)
                                }
                                onPreviewRequested: {
                                    root.previewRequested(model.factorId)
                                }
                                onAnalyzeRequested: {
                                    root.analyzeRequested(model.factorId)
                                }
                                onAddToPortfolio: {
                                    root.addToPortfolio(model.factorId)
                                }
                                onEditRequested: {
                                    root.editRequested(model.factorId)
                                }
                                onDeleteRequested: {
                                    if (root.factorMutationInProgress) {
                                        return
                                    }
                                    // 直接发出删除请求信号，不显示确认对话框
                                    root.deleteRequested(model.factorId)
                                }
                            }
                            
                            ScrollBar.vertical: ScrollBar {
                                policy: ScrollBar.AsNeeded
                                width: 0
                                visible: false
                            }
                        }
                        
                        // 列表视图
                        ListView {
                            id: listView
                            anchors.fill: parent
                            clip: true
                            visible: viewMode === "list"
                            spacing: 12
                            
                            model: factorModel
                            
                            delegate: FactorComponents.FactorListRow {
                                width: listView.width - 20
                                height: 80
                                
                                factorId: model.factorId
                                displayName: model.displayName
                                majorCategory: model.majorCategory
                                icValue: model.icValue
                                irValue: model.irValue
                                turnoverRate: model.turnoverRate
                                isFavorite: model.isFavorite
                                status: model.status
                                
                                selected: root.selectedFactorId === model.factorId
                                showActions: true
                                
                                onClicked: {
                                    root.factorSelected(model.factorId)
                                }
                                onDoubleClicked: {
                                    root.factorDoubleClicked(model.factorId)
                                }
                                onFavoriteToggled: {
                                    root.favoriteToggled(model.factorId, favorite)
                                }
                                onAnalyzeRequested: {
                                    root.analyzeRequested(model.factorId)
                                }
                                onAddToPortfolio: {
                                    root.addToPortfolio(model.factorId)
                                }
                            }
                            
                            ScrollBar.vertical: ScrollBar {
                                policy: ScrollBar.AsNeeded
                                width: 0
                                visible: false
                            }
                        }
                        
                        // 对比视图
                        Rectangle {
                            anchors.fill: parent
                            color: "#0F172A"
                            visible: viewMode === "compare"
                            
                            Text {
                                anchors.centerIn: parent
                                text: "对比视图 (开发中)"
                                font.pixelSize: 24
                                color: "#94A3B8"
                            }
                        }
                    }
                }
            }
        }
    }
    
    // ============ 内部函数 ============
    
    function startNaturalLanguageSearch() {
        console.log("启动自然语言搜索")
        // TODO: 实现自然语言搜索功能
    }
    
    function openICFilterDialog() {
        console.log("打开IC筛选对话框")
        // TODO: 实现IC筛选对话框
    }
    
    function openTurnoverFilterDialog() {
        console.log("打开换手率筛选对话框")
        // TODO: 实现换手率筛选对话框
    }
    
    function addFilterCondition() {
        console.log("添加筛选条件")
        // TODO: 实现添加筛选条件功能
    }
    
    // 根据因子ID获取因子名称（简化版，不访问factorModel.get()）
    function getFactorNameById(factorId) {
        if (!factorModel) {
            return "未知因子"
        }
        
        // 简化实现：直接从model中查找（使用modelData属性）
        if (factorModel && typeof factorModel.findFactorById === "function") {
            var factor = factorModel.findFactorById(factorId)
            if (factor && !factor.isEmpty) {
                return factor.displayName || factor.factorName || "未知因子"
            }
        }
        
        // 如果无法找到，返回通用名称
        return "因子-" + factorId
    }
    
    // 执行删除因子
    function executeDeleteFactor(factorId) {
        if (!factorService) {
            console.error("因子服务未初始化")
            deleteCompleted(factorId, false, "因子服务未初始化")
            return
        }

        if (root.factorMutationInProgress) {
            console.warn("因子服务正在处理写操作，跳过重复删除:", factorId)
            deleteCompleted(factorId, false, "因子服务正在处理其他写操作")
            return
        }
        
        console.log("执行删除因子:", factorId)
        
        try {
            // 调用因子服务的删除方法
            var success = factorService.deleteFactor(factorId)
            
            // 处理删除结果
            if (success) {
                console.log("✅ 因子删除成功:", factorId)
                // 发送删除完成信号（成功）
                deleteCompleted(factorId, true, "因子删除成功")
            } else {
                console.error("❌ 因子删除失败:", factorId)
                // 发送删除完成信号（失败）
                deleteCompleted(factorId, false, "因子删除失败，请检查数据库连接")
            }
        } catch (error) {
            console.error("❌ 删除因子时发生异常:", error)
            deleteCompleted(factorId, false, "删除异常: " + error)
        }
    }
    
    // ============ 初始化 ============

    Component.onCompleted: {
        console.log("FactorLibraryPage: 组件初始化完成")
    }
    
    // 删除确认对话框
    MessageDialog {
        id: deleteConfirmDialog
        title: "确认删除"
        property string factorId: ""
        property string factorName: ""
        
        onAccepted: {
            console.log("用户确认删除因子:", factorId, factorName)
            // 执行删除操作
            executeDeleteFactor(factorId)
        }
        
        onRejected: {
            console.log("用户取消删除因子:", factorId, factorName)
            // 发送删除取消信号
            deleteCompleted(factorId, false, "用户取消删除")
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.factorMutationInProgress
        z: 100
        color: "#020617"
        opacity: 0.38
    }

    MouseArea {
        anchors.fill: parent
        visible: root.factorMutationInProgress
        enabled: root.factorMutationInProgress
        z: 101
        cursorShape: Qt.BusyCursor
    }

    Column {
        anchors.centerIn: parent
        visible: root.factorMutationInProgress
        z: 102
        spacing: 10

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            running: root.factorMutationInProgress
            width: 42
            height: 42
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "因子服务正在写入，请稍候"
            font.pixelSize: 14
            font.bold: true
            color: "#F8FAFC"
        }
    }
    
}
