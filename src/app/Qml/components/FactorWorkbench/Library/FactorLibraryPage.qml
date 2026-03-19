// FactorLibraryPage.qml
// 因子库模块 - FactorWorkbench的因子库浏览页面
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 
import "../../Factor" as FactorComponents

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
                            color: "#10B981"
                            
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
                                cursorShape: Qt.PointingHandCursor
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
                            cellWidth: Math.floor(parent.width / 3) - 15  // 每行3个，减去边距（因为卡片更窄了）
                            cellHeight: 280  // 匹配卡片高度(260) + 边距(20)
                            clip: true
                            visible: viewMode === "grid"
                            
                            model: factorModel
                            
                            delegate: FactorComponents.FactorCardEnhanced {
                                width: gridView.cellWidth - 15  // 边距
                                height: gridView.cellHeight - 20  // 边距
                                
                                factorId: model.factorId
                                factorName: model.factorName
                                displayName: model.displayName
                                majorCategory: model.majorCategory
                                subCategory: model.subCategory
                                description: model.description
                                icValue: model.icValue
                                irValue: model.irValue
                                validityDays: model.validityDays
                                turnoverRate: model.turnoverRate
                                isRecommended: model.isRecommended
                                isFavorite: model.isFavorite
                                status: model.status
                                tags: model.tags
                                creator: model.creator
                                createDate: model.createDate
                                
                                selected: selectedFactorId === model.factorId
                                showMiniChart: true
                                showGroupReturns: true
                                groupReturns: model.groupReturns
                                
                                onClicked: {
                                    selectedFactorId = model.factorId
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
                                
                                selected: selectedFactorId === model.factorId
                                showActions: true
                                
                                onClicked: {
                                    selectedFactorId = model.factorId
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
    
    // 根据因子ID获取因子名称
    function getFactorNameById(factorId) {
        if (!factorModel) {
            return "未知因子"
        }
        
        for (var i = 0; i < factorModel.count; i++) {
            var factor = factorModel.get(i)
            if (factor.factorId === factorId) {
                return factor.displayName || factor.factorName
            }
        }
        
        return "未知因子"
    }
    
    // ============ 初始化 ============

    Component.onCompleted: {
        console.log("FactorLibraryPage: 组件初始化完成")
        // 这里可以添加组件初始化逻辑
    }
    
}
