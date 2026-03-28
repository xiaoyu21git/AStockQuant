// FactorBacktestPage.qml
// 因子回测页面 - 重新设计版本
// 专注于回测进度监控和分组内容展示
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge

/**
 * 因子回测页面组件 - 重新设计版本
 * 专注于回测进度监控和分组内容展示
 */
Item {
    id: root

    function resolveFactorDisplayName(factorId) {
        if (!factorId) {
            return ""
        }

        if (factorService && factorService.getViewModel) {
            var viewModel = factorService.getViewModel()
            if (viewModel && viewModel.getFactorById) {
                var factorInfo = viewModel.getFactorById(String(factorId))
                if (factorInfo) {
                    return factorInfo.displayName || factorInfo.factorName || factorInfo.name || String(factorId)
                }
            }
        }

        return String(factorId)
    }

    function selectedFactorDisplayText() {
        if (!selectedFactorIds || selectedFactorIds.length === 0) {
            return "未选择因子"
        }

        var names = []
        for (var i = 0; i < selectedFactorIds.length; i++) {
            names.push(resolveFactorDisplayName(selectedFactorIds[i]))
        }

        if (names.length <= 3) {
            return names.join("、")
        }

        return names.slice(0, 3).join("、") + " 等 " + names.length + " 个因子"
    }

    function hasMetricValue(value) {
        return value !== undefined && value !== null
    }

    function formatMetric(value, digits) {
        return hasMetricValue(value) ? Number(value).toFixed(digits) : Number(0).toFixed(digits)
    }

    function formatPercentMetric(value, digits) {
        return hasMetricValue(value) ? (Number(value) * 100).toFixed(digits) + "%" : (Number(0) * 100).toFixed(digits) + "%"
    }

    function formatTextMetric(value, fallback) {
        return hasMetricValue(value) && String(value).length > 0 ? String(value) : fallback
    }

    function applyDisplayedBacktestResult(result) {
        if (!result) {
            root.backtestResult = ({})
            root.groupResults = []
            root.icirResult = ({})
            root.summaryStats = ({})
            return
        }

        root.backtestResult = result

        if (result.results && Array.isArray(result.results)) {
            if (result.results.length > 0) {
                var firstResult = result.results[0]
                root.groupResults = firstResult && firstResult.groups && Array.isArray(firstResult.groups) ? firstResult.groups : []
                root.icirResult = firstResult && firstResult.icirResult ? firstResult.icirResult : ({})
                root.summaryStats = firstResult && firstResult.summary ? firstResult.summary : ({})
            } else {
                root.groupResults = []
                root.icirResult = ({})
                root.summaryStats = ({})
            }
            return
        }

        root.groupResults = result.groups && Array.isArray(result.groups) ? result.groups : []
        root.icirResult = result.icirResult ? result.icirResult : ({})
        root.summaryStats = result.summary ? result.summary : ({})
    }
    
    // ============ 属性 ============
    
    property Bridge.FactorService factorService: null
    property Bridge.CleanedDataController cleanedDataController: null
    
    // 因子选择相关属性 - 现在由C++控制器管理
    property var selectedFactorIds: []  // 支持多因子选择，与控制器同步
    property string selectedFactorId: ""  // 向后兼容，取第一个选中的因子
    
    // 因子选择对话框
    property var factorSelectorDialog: null
    
    // 数据集模型 - 不再使用，由C++控制器自动处理缓存
    
    // 回测控制器 - 使用属性绑定
    Bridge.FactorBacktestController {
        id: factorBacktestController
        
        // 绑定回测状态到QML属性
        onIsRunningChanged: {
            root.isBacktesting = factorBacktestController.isRunning
        }
        onProgressChanged: {
            root.backtestProgress = factorBacktestController.progress
        }
        onStatusChanged: {
            root.backtestStatus = factorBacktestController.status
        }
        
        // 绑定回测结果到QML属性
        onGroupResultsChanged: {
            root.groupResults = factorBacktestController.groupResults
        }
        onIcirResultChanged: {
            root.icirResult = factorBacktestController.icirResult
        }
        onSummaryStatsChanged: {
            root.summaryStats = factorBacktestController.summaryStats
        }
        onBacktestResultChanged: {
            if (!root.isBacktesting) {
                root.applyDisplayedBacktestResult(factorBacktestController.backtestResult)
            }
        }
        
        onBacktestStarted: function(factorId) {
            console.log("回测开始:", factorId)
            root.lastBacktestError = ""
            showToast("▶️ 回测开始")
        }
        onBacktestProgress: function(progress, status) {
            // 进度信息已通过属性绑定更新
        }
        onBacktestProgressDetailed: function(progress, status, currentGroupNum, totalGroupsNum) {
            root.currentGroup = currentGroupNum
            root.totalGroups = totalGroupsNum
        }
        onBacktestCompleted: function(result) {
            console.log("📊 回测完成信号收到!")
            console.log("📊 result keys:", result ? Object.keys(result) : "null")
            root.lastBacktestError = ""
            root.applyDisplayedBacktestResult(result)
            
            root.currentGroup = 0
            root.totalGroups = 0
            showToast("✅ 因子回测完成")
            
            // 打印最终状态
            console.log("📊 最终 groupResults 数量:", root.groupResults.length)
            console.log("📊 最终 icirResult:", root.icirResult)
            console.log("📊 最终 summaryStats:", root.summaryStats)
        }
        onBacktestFailed: function(error) {
            console.error("回测失败:", error)
            root.currentGroup = 0
            root.totalGroups = 0
            root.backtestResult = ({})
            root.groupResults = []
            root.icirResult = ({})
            root.summaryStats = ({})
            root.lastBacktestError = error
            showToast("❌ 回测失败: " + error)
        }
        onBacktestCancelled: function() {
            console.log("回测已取消")
            root.currentGroup = 0
            root.totalGroups = 0
            root.lastBacktestError = ""
            showToast("⏸️ 回测已取消")
        }
    }
    
    // 回测状态
    property bool isBacktesting: false
    property int backtestProgress: 0
    property string backtestStatus: "等待开始"
    property int currentGroup: 0
    property int totalGroups: 0
    
    // 回测结果
    property var backtestResult: ({})
    property var groupResults: []
    property var icirResult: ({})
    property var summaryStats: ({})
    property string lastBacktestError: ""
    
    // 分组配置
    property var groupConfig: ({})
    
    // 数据源属性
    property int selectedDatasetId: -1
    property string selectedDataSourceMode: "cache"
    property var cacheDatasetOptions: [{ text: "请选择缓存集", value: -1, raw: null }]

    function rebuildCacheDatasetOptions() {
        var options = [{ text: "请选择缓存集", value: -1, raw: null }]

        if (cleanedDataController && cleanedDataController.datasetList) {
            var datasets = cleanedDataController.datasetList
            for (var i = 0; i < datasets.length; i++) {
                var dataset = datasets[i]
                if (!dataset || dataset.id === undefined) {
                    continue
                }
                if (!dataset.isBacktestReady) {
                    continue
                }

                var parts = []
                parts.push("#" + dataset.id)
                parts.push(dataset.displayName || dataset.name || "未命名缓存集")
                if (dataset.startDate && dataset.endDate) {
                    parts.push("(" + dataset.startDate + "~" + dataset.endDate + ")")
                }

                options.push({
                    text: parts.join(" "),
                    value: dataset.id,
                    raw: dataset
                })
            }
        }

        cacheDatasetOptions = options
        syncSelectedDatasetIndex()
        console.log("回测页缓存集选项已刷新，数量:", cacheDatasetOptions.length)
    }

    function syncSelectedDatasetIndex() {
        if (!datasetComboBox) {
            return
        }

        var options = cacheDatasetOptions
        if (!options || options.length === 0) {
            return
        }

        var targetId = selectedDatasetId
        if (targetId < 0 && cleanedDataController.selectedDatasetInfo && cleanedDataController.selectedDatasetInfo.id !== undefined) {
            targetId = cleanedDataController.selectedDatasetInfo.id
        }

        for (var index = 0; index < options.length; index++) {
            if (options[index].value === targetId) {
                if (datasetComboBox.currentIndex !== index) {
                    datasetComboBox.currentIndex = index
                }
                return
            }
        }

        if (datasetComboBox.currentIndex < 0 && options.length > 0) {
            datasetComboBox.currentIndex = 0
        }
    }

    onVisibleChanged: {
        if (visible && cleanedDataController) {
            cleanedDataController.refreshDatasets()
            rebuildCacheDatasetOptions()
        }
    }
    
    // ============ UI ============
    
    Rectangle {
        anchors.fill: parent
        color: "#0F172A"
        
        Flickable {
            id: scrollView
            anchors.fill: parent
            anchors.margins: 16
            clip: true
            contentWidth: contentColumn.width
            contentHeight: contentColumn.height
            boundsBehavior: Flickable.StopAtBounds
            
            // 滚动条
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                width: 8
            }
            
            ColumnLayout {
                id: contentColumn
                width: scrollView.width
                spacing: 16
            
                // 标题区域
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    
                    Text {
                        text: "🧪 因子回测"
                        font.pixelSize: 24
                        font.weight: Font.Bold
                        color: "#F1F5F9"
                    }
                    
                    Text {
                        text: "验证因子预测能力，监控回测进度"
                        font.pixelSize: 12
                        color: "#94A3B8"
                    }
                    
                    Item { Layout.fillWidth: true }
                }
                
                // 回测控制面板
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 180
                    radius: 12
                    color: "#1E293B"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12
                        
                        // 因子选择区域
                        RowLayout {
                            spacing: 12
                            
                            // 选择因子按钮
                            Rectangle {
                                Layout.preferredWidth: 140
                                Layout.preferredHeight: 40
                                radius: 8
                                color: "#3B82F6"
                                
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    
                                    Text {
                                        text: "📊"
                                        font.pixelSize: 14
                                        color: "white"
                                    }
                                    
                                    Text {
                                        text: "选择因子"
                                        font.pixelSize: 14
                                        font.weight: Font.Medium
                                        color: "white"
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: openFactorSelector()
                                }
                            }
                            
                            // 占位空间
                            Item {
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 40
                            }
                            
                            // 已选因子显示
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                
                                Repeater {
                                    model: selectedFactorIds
                                    
                                    delegate: Rectangle {
                                        height: 28
                                        radius: 14
                                        color: "#3B82F620"
                                        
                                        Row {
                                            spacing: 6
                                            anchors.centerIn: parent
                                            
                                            Text {
                                                text: root.resolveFactorDisplayName(modelData)
                                                font.pixelSize: 11
                                                color: "#3B82F6"
                                                leftPadding: 10
                                            }
                                            
                                            Text {
                                                text: "×"
                                                font.pixelSize: 12
                                                color: "#3B82F6"
                                                rightPadding: 10
                                                
                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: {
                                                        var index = selectedFactorIds.indexOf(modelData)
                                                        if (index !== -1) {
                                                            selectedFactorIds.splice(index, 1)
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                
                                // 空状态提示
                                Text {
                                    text: selectedFactorIds.length === 0 ? "请选择要回测的因子" : ""
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                    visible: selectedFactorIds.length === 0
                                }
                            }
                        }
                        
                        // 回测配置
                        RowLayout {
                            spacing: 16
                            
                            // 分组数量
                            ColumnLayout {
                                spacing: 4
                                
                                Text {
                                    text: "分组数量"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }
                                
                                ComboBox {
                                    id: groupComboBox
                                    Layout.preferredWidth: 80
                                    model: ["5组", "10组", "20组"]
                                    currentIndex: 1
                                    
                                    background: Rectangle {
                                        radius: 6
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#334155"
                                    }
                                    
                                    contentItem: Text {
                                        text: parent.displayText
                                        font.pixelSize: 12
                                        color: "#F1F5F9"
                                        horizontalAlignment: Text.AlignLeft
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }
                            
                            // 数据源选择
                            ColumnLayout {
                                spacing: 4
                                
                                Text {
                                    text: "数据源"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }
                                
                                ComboBox {
                                    id: dataSourceComboBox
                                    Layout.preferredWidth: 140
                                    model: [
                                        { text: "缓存集", value: "cache" },
                                        { text: "数据库", value: "database" }
                                    ]
                                    textRole: "text"

                                    background: Rectangle {
                                        radius: 6
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: "#334155"
                                    }

                                    contentItem: Text {
                                        text: dataSourceComboBox.displayText
                                        font.pixelSize: 12
                                        color: "#F1F5F9"
                                        verticalAlignment: Text.AlignVCenter
                                    }

                                    onCurrentIndexChanged: {
                                        if (currentIndex < 0 || currentIndex >= model.length) {
                                            return
                                        }

                                        selectedDataSourceMode = model[currentIndex].value
                                        factorBacktestController.dataSourceMode = selectedDataSourceMode
                                    }
                                }

                                Text {
                                    text: selectedDataSourceMode === "cache"
                                          ? "使用用户选择的缓存集范围和股票池"
                                          : "直接从数据库读取全量回测数据"
                                    font.pixelSize: 10
                                    color: "#64748B"
                                }
                            }

                            ColumnLayout {
                                spacing: 4

                                Text {
                                    text: "缓存集"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }

                                ComboBox {
                                    id: datasetComboBox
                                    Layout.preferredWidth: 320
                                    model: cacheDatasetOptions
                                    textRole: "text"
                                    enabled: selectedDataSourceMode === "cache"
                                    opacity: enabled ? 1.0 : 0.45

                                    background: Rectangle {
                                        radius: 6
                                        color: "#0F172A"
                                        border.width: 1
                                        border.color: enabled ? "#334155" : "#1E293B"
                                    }

                                    contentItem: Text {
                                        text: datasetComboBox.displayText
                                        font.pixelSize: 12
                                        color: "#F1F5F9"
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }

                                    onCurrentIndexChanged: {
                                        if (selectedDataSourceMode !== "cache") {
                                            return
                                        }

                                        if (!cleanedDataController || currentIndex < 0 || currentIndex >= model.length) {
                                            return
                                        }

                                        var selected = model[currentIndex]
                                        if (!selected || selected.value === undefined || selected.value <= 0) {
                                            return
                                        }

                                        if (selectedDatasetId !== selected.value) {
                                            selectedDatasetId = selected.value
                                        }

                                        factorBacktestController.selectedDatasetId = selected.value
                                        cleanedDataController.loadDatasetById(selected.value)
                                        console.log("回测页选择缓存集:", selected.value, selected.text)
                                    }
                                }

                                Text {
                                    text: selectedDatasetId > 0
                                          ? "当前缓存集 key: " + selectedDatasetId
                                          : (cacheDatasetOptions.length > 1 ? "请先选择一个缓存集" : "当前没有可用缓存集")
                                    font.pixelSize: 10
                                    color: selectedDatasetId > 0 ? "#93C5FD" : "#64748B"
                                }

                                Text {
                                    text: {
                                        for (var i = 0; i < cacheDatasetOptions.length; i++) {
                                            if (cacheDatasetOptions[i].value === selectedDatasetId && cacheDatasetOptions[i].raw) {
                                                return "当前缓存集 value: " + (cacheDatasetOptions[i].raw.displayName || cacheDatasetOptions[i].raw.name || "")
                                            }
                                        }
                                        return ""
                                    }
                                    visible: text.length > 0
                                    font.pixelSize: 10
                                    color: "#64748B"
                                }
                            }
                            
                            
                            Item { Layout.fillWidth: true }
                            
                            // 回测按钮
                            Rectangle {
                                id: backtestButton
                                Layout.preferredWidth: 120
                                Layout.preferredHeight: 40
                                radius: 8
                                color: isBacktesting ? "#334155" : (selectedFactorIds.length > 0 ? "#3B82F6" : "#475569")
                                
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    
                                    Text {
                                        text: isBacktesting ? "⏸️" : "▶️"
                                        font.pixelSize: 14
                                        color: isBacktesting ? "#94A3B8" : (selectedFactorIds.length > 0 ? "white" : "#94A3B8")
                                    }
                                    
                                    Text {
                                        text: isBacktesting ? "回测中..." : "开始回测"
                                        font.pixelSize: 14
                                        font.weight: Font.Medium
                                        color: isBacktesting ? "#94A3B8" : (selectedFactorIds.length > 0 ? "white" : "#94A3B8")
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    enabled: !isBacktesting && selectedFactorIds.length > 0
                                    onClicked: startBacktest()
                                }
                            }
                            
                            // 取消按钮
                            Rectangle {
                                Layout.preferredWidth: 80
                                Layout.preferredHeight: 40
                                radius: 8
                                color: "#334155"
                                visible: isBacktesting
                                
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    
                                    Text {
                                        text: "✕"
                                        font.pixelSize: 14
                                        color: "#EF4444"
                                    }
                                    
                                    Text {
                                        text: "取消"
                                        font.pixelSize: 14
                                        color: "#EF4444"
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: factorBacktestController.cancelBacktest()
                                }
                            }
                        }
                        
                        // 进度区域
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            visible: isBacktesting
                            
                            // 进度条
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 8
                                radius: 4
                                color: "#334155"
                                
                                Rectangle {
                                    width: parent.width * (backtestProgress / 100)
                                    height: parent.height
                                    radius: 4
                                    color: "#3B82F6"
                                }
                            }
                            
                            // 进度信息
                            RowLayout {
                                Layout.fillWidth: true
                                
                                Text {
                                    text: backtestStatus
                                    font.pixelSize: 12
                                    color: "#F59E0B"
                                }
                                
                                Text {
                                    text: backtestProgress + "%"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }
                                
                                Item { Layout.fillWidth: true }
                                
                                Text {
                                    text: currentGroup > 0 ? "分组: " + currentGroup + "/" + totalGroups : ""
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                    visible: currentGroup > 0
                                }
                            }
                        }
                    }
                }
            
                // 主要内容区域
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 500  // 使用固定高度让Flickable可以滚动
                    spacing: 16
                    
                    // 分组内容展示
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: parent.width * 0.6
                        radius: 12
                        color: "#1E293B"
                        
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12
                            
                            // 标题
                            RowLayout {
                                Layout.fillWidth: true
                                
                                Text {
                                    text: "📊 分组内容"
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                    color: "#F1F5F9"
                                }
                                
                                Item { Layout.fillWidth: true }
                                
                                Text {
                                    text: groupResults.length > 0 ? "共 " + groupResults.length + " 个分组" : "等待回测结果"
                                    font.pixelSize: 12
                                    color: "#94A3B8"
                                }
                            }
                            
                            // 分组列表
                            ListView {
                                id: groupListView
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                model: groupResults
                                clip: true
                                spacing: 8
                                
                                delegate: Rectangle {
                                    width: groupListView.width
                                    height: 60
                                    radius: 8
                                    color: "#1E293B"
                                    
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        spacing: 12
                                        
                                        // 分组编号
                                        Rectangle {
                                            Layout.preferredWidth: 32
                                            Layout.preferredHeight: 32
                                            radius: 16
                                            color: "#0F172A"
                                            
                                            Text {
                                                anchors.centerIn: parent
                                                text: modelData.groupId || (index + 1)
                                                font.pixelSize: 12
                                                font.weight: Font.Bold
                                                color: "#F1F5F9"
                                            }
                                        }
                                        
                                        // 分组信息
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            
                                            Text {
                                                text: modelData.groupName || ("第 " + (index + 1) + " 组")
                                                font.pixelSize: 14
                                                font.weight: Font.Medium
                                                color: "#F1F5F9"
                                            }
                                            
                                            RowLayout {
                                                spacing: 16
                                                
                                                Text {
                                                    text: "股票: " + (modelData.stockCount || 0)
                                                    font.pixelSize: 11
                                                    color: "#94A3B8"
                                                }
                                                
                                                Text {
                                                    text: "因子值: " + (modelData.minFactorValue || 0).toFixed(2) + " - " + (modelData.maxFactorValue || 0).toFixed(2)
                                                    font.pixelSize: 11
                                                    color: "#94A3B8"
                                                }
                                            }
                                        }
                                        
                                        // 收益信息
                                        ColumnLayout {
                                            Layout.alignment: Qt.AlignRight
                                            spacing: 2
                                            
                                            Text {
                                                text: (((modelData.return || 0) * 100)).toFixed(2) + "%"
                                                font.pixelSize: 16
                                                font.weight: Font.Bold
                                                color: (modelData.return || 0) > 0 ? "#EF4444" : ((modelData.return || 0) < 0 ? "#10B981" : "#94A3B8")
                                            }
                                            
                                            Text {
                                                text: "收益"
                                                font.pixelSize: 10
                                                color: "#94A3B8"
                                            }
                                        }
                                    }
                                    
                                    // 当前分组高亮
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 8
                                        color: "#3B82F620"
                                        border.width: 2
                                        border.color: "#3B82F6"
                                        visible: isBacktesting && currentGroup === (index + 1)
                                    }
                                }
                                
                                // 空状态
                                Text {
                                    anchors.centerIn: parent
                                    text: isBacktesting ? "正在计算分组..." : "请开始回测查看分组内容"
                                    font.pixelSize: 14
                                    color: "#94A3B8"
                                    visible: groupResults.length === 0
                                }
                            }
                        }
                    }
                    
                    // 分析报告区域
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: parent.width * 0.4
                        radius: 12
                        color: "#1E293B"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12

                            ScrollView {
                                id: analysisReportScrollView
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true

                                ColumnLayout {
                                    width: analysisReportScrollView.availableWidth
                                    spacing: 12

                                    RowLayout {
                                        Layout.fillWidth: true

                                        Text {
                                            text: "📈 分析报告"
                                            font.pixelSize: 16
                                            font.weight: Font.DemiBold
                                            color: "#F1F5F9"
                                        }

                                        Item { Layout.fillWidth: true }

                                        Text {
                                            text: root.selectedFactorDisplayText()
                                            font.pixelSize: 12
                                            color: "#94A3B8"
                                        }
                                    }

                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: 2
                                        columnSpacing: 12
                                        rowSpacing: 12

                                        KeyMetricCard {
                                            title: "IC值"
                                            value: root.formatMetric(icirResult.icValue, 3)
                                            description: "信息系数"
                                            color: "#F1F5F9"
                                            trend: "neutral"
                                        }

                                        KeyMetricCard {
                                            title: "IR值"
                                            value: root.formatMetric(icirResult.irValue, 2)
                                            description: "信息比率"
                                            color: "#F1F5F9"
                                            trend: "neutral"
                                        }

                                        KeyMetricCard {
                                            title: "显著性"
                                            value: icirResult.isSignificant ? "显著" : "不显著"
                                            description: "统计显著性"
                                            color: icirResult.isSignificant ? "#10B981" : "#EF4444"
                                            trend: icirResult.isSignificant ? "up" : "down"
                                        }

                                        KeyMetricCard {
                                            title: "多空收益差"
                                            value: root.formatPercentMetric(summaryStats.spreadReturn, 2)
                                            description: "Top-Bottom Spread"
                                            color: "#F1F5F9"
                                            trend: "neutral"
                                        }

                                        KeyMetricCard {
                                            title: "胜率"
                                            value: root.formatPercentMetric(summaryStats.winRate, 1)
                                            description: "Win Rate"
                                            color: "#F1F5F9"
                                            trend: "neutral"
                                        }

                                        KeyMetricCard {
                                            title: "夏普比率"
                                            value: root.formatMetric(summaryStats.sharpeRatio, 2)
                                            description: "Sharpe Ratio"
                                            color: "#F1F5F9"
                                            trend: "neutral"
                                        }

                                        KeyMetricCard {
                                            title: "最大回撤"
                                            value: root.formatPercentMetric(summaryStats.maxDrawdown, 2)
                                            description: "Max Drawdown"
                                            color: "#F1F5F9"
                                            trend: "neutral"
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 120
                                        radius: 8
                                        color: "#0F172A"

                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 12
                                            spacing: 8

                                            Text {
                                                text: "💡 智能结论"
                                                font.pixelSize: 12
                                                font.weight: Font.Medium
                                                color: "#F1F5F9"
                                            }

                                            Text {
                                                text: icirResult.conclusion || (root.lastBacktestError ? ("当前回测未成功：" + root.lastBacktestError) : "请完成回测以获取因子分析结论。")
                                                font.pixelSize: 11
                                                color: "#94A3B8"
                                                wrapMode: Text.WordWrap
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                            }

                                            Rectangle {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 34
                                                radius: 6
                                                color: "#111827"

                                                RowLayout {
                                                    anchors.fill: parent
                                                    anchors.leftMargin: 10
                                                    anchors.rightMargin: 10

                                                    Text {
                                                        text: "报告状态"
                                                        font.pixelSize: 11
                                                        color: "#94A3B8"
                                                    }

                                                    Item { Layout.fillWidth: true }

                                                    Text {
                                                        text: root.lastBacktestError ? "待修复" : "已生成"
                                                        font.pixelSize: 11
                                                        font.weight: Font.Medium
                                                        color: root.lastBacktestError ? "#F59E0B" : "#10B981"
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 220
                                        radius: 8
                                        color: "#0F172A"

                                        GridLayout {
                                            anchors.fill: parent
                                            anchors.margins: 12
                                            columns: 2
                                            columnSpacing: 16
                                            rowSpacing: 8

                                            Text {
                                                text: "参数结果"
                                                font.pixelSize: 12
                                                font.weight: Font.Medium
                                                color: "#F1F5F9"
                                                Layout.columnSpan: 2
                                            }

                                            Text { text: "因子"; font.pixelSize: 11; color: "#94A3B8" }
                                            Text { text: root.formatTextMetric(backtestResult.config ? backtestResult.config.factorName : root.selectedFactorDisplayText(), "--"); font.pixelSize: 11; color: "#F1F5F9" }

                                            Text { text: "数据源"; font.pixelSize: 11; color: "#94A3B8" }
                                            Text { text: root.formatTextMetric(backtestResult.config ? backtestResult.config.dataSourceMode : undefined, "--"); font.pixelSize: 11; color: "#F1F5F9" }

                                            Text { text: "时间范围"; font.pixelSize: 11; color: "#94A3B8" }
                                            Text { text: root.formatTextMetric(backtestResult.config ? ((backtestResult.config.startDate || "--") + " ~ " + (backtestResult.config.endDate || "--")) : undefined, "--"); font.pixelSize: 11; color: "#F1F5F9" }

                                            Text { text: "分组数 / 持有期"; font.pixelSize: 11; color: "#94A3B8" }
                                            Text { text: root.formatTextMetric(backtestResult.config ? ((backtestResult.config.numGroups || "--") + " 组 / " + (backtestResult.config.forwardDays || "--") + " 天") : undefined, "--"); font.pixelSize: 11; color: "#F1F5F9" }

                                            Text { text: "交易成本"; font.pixelSize: 11; color: "#94A3B8" }
                                            Text { text: root.formatPercentMetric(backtestResult.config ? backtestResult.config.transactionCost : undefined, 2); font.pixelSize: 11; color: "#F1F5F9" }

                                            Text { text: "数据覆盖率"; font.pixelSize: 11; color: "#94A3B8" }
                                            Text { text: root.formatPercentMetric(summaryStats.dataCoverage, 1); font.pixelSize: 11; color: "#F1F5F9" }

                                            Text { text: "IC正率 / 年化收益"; font.pixelSize: 11; color: "#94A3B8" }
                                            Text { text: root.formatPercentMetric(icirResult.icPositiveRate, 1) + " / " + root.formatPercentMetric(summaryStats.annualReturn, 2); font.pixelSize: 11; color: "#F1F5F9" }
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 32
                                    radius: 6
                                    color: "#334155"

                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 6

                                        Text { text: "📈"; font.pixelSize: 12; color: "#F1F5F9" }
                                        Text { text: "详细报告"; font.pixelSize: 12; color: "#F1F5F9" }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: showDetailedReport()
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 32
                                    radius: 6
                                    color: "#3B82F6"

                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 6

                                        Text { text: "📤"; font.pixelSize: 12; color: "white" }
                                        Text { text: "导出结果"; font.pixelSize: 12; color: "white" }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: exportResults()
                                    }
                                }
                            }
                        }
                    }
                }
            } // 这里应该是ColumnLayout的结束
        } // 这里应该是Flickable的结束，这是修复的关键位置
    } // 这是最外层Rectangle的结束
    
    // ============ 组件定义 ============
    
    // 关键指标卡片组件
    component KeyMetricCard: Item {
        property string title: ""
        property string value: ""
        property string description: ""
        property string color: "#F1F5F9"
        property string trend: "neutral"
        
        Layout.fillWidth: true
        Layout.preferredHeight: 70
        
        Rectangle {
            anchors.fill: parent
            radius: 8
            color: "#0F172A"
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2
                
                Text {
                    text: title
                    font.pixelSize: 10
                    color: "#94A3B8"
                }
                
                Row {
                    spacing: 4
                    
                    Text {
                        text: value
                        font.pixelSize: 16
                        font.weight: Font.Bold
                        color: parent.parent.parent.color
                    }
                    
                    // 趋势指示器
                    Text {
                        visible: trend !== "neutral"
                        text: trend === "up" ? "↑" : "↓"
                        font.pixelSize: 12
                        color: trend === "up" ? "#10B981" : "#EF4444"
                    }
                }
                
                Text {
                    text: description
                    font.pixelSize: 9
                    color: "#64748B"
                }
            }
        }
    }
    
    // ============ 内部函数 ============
    // 所有复杂逻辑已移至C++控制器，QML只负责UI显示和信号处理
    
    // 开始回测 - 简化版本，只调用C++控制器
    function startBacktest() {
        console.log("开始回测，因子数量:", selectedFactorIds.length)
        
        if (selectedFactorIds.length === 0) {
            console.log("请先选择要回测的因子")
            return
        }
        
        // 首先将选择的因子ID传递给控制器
        if (factorBacktestController) {
            var selectedStartDate = ""
            var selectedEndDate = ""

            if (cleanedDataController) {
                if (cleanedDataController.currentStartDate && cleanedDataController.currentEndDate) {
                    selectedStartDate = cleanedDataController.currentStartDate
                    selectedEndDate = cleanedDataController.currentEndDate
                } else {
                    var dateRange = cleanedDataController.getDataDateRange()
                    if (dateRange && dateRange.startDate && dateRange.endDate) {
                        selectedStartDate = dateRange.startDate
                        selectedEndDate = dateRange.endDate
                    }
                }

                console.log("回测使用的数据集:", JSON.stringify(cleanedDataController.selectedDatasetInfo))
            }

            console.log("回测日期范围:", selectedStartDate, "至", selectedEndDate)

            // 将JavaScript数组转换为QVariantList
            var factorIdList = []
            for (var i = 0; i < selectedFactorIds.length; i++) {
                factorIdList.push(selectedFactorIds[i])
            }
            
            // 直接设置控制器的selectedFactorIds属性（而不是调用方法）
            factorBacktestController.selectedFactorIds = factorIdList
            factorBacktestController.selectedDatasetId = selectedDatasetId
            factorBacktestController.dataSourceMode = selectedDataSourceMode
            
            // 调用C++控制器开始回测，传递当前选中数据集对应的日期范围
            factorBacktestController.startBacktest(groupComboBox.currentText, selectedStartDate, selectedEndDate)
        }
    }
    
    // 打开因子选择对话框 - 简化版本
    function openFactorSelector() {
        console.log("打开因子选择对话框")
        
        // 创建对话框组件
        var component = Qt.createComponent("FactorSelectorDialog.qml")
        if (component.status === Component.Ready) {
            factorSelectorDialog = component.createObject(root, {
                factorViewModel: factorService ? factorService.getViewModel() : null,
                selectedFactorIds: selectedFactorIds.slice()
            })
            
            // 连接信号
            factorSelectorDialog.factorsSelected.connect(handleFactorsSelected)
            factorSelectorDialog.dialogClosed.connect(handleDialogClosed)
            
            factorSelectorDialog.open()
        } else {
            console.error("无法创建因子选择对话框组件:", component.errorString())
        }
    }
    
    // 处理因子选择结果 - 简化版本
    function handleFactorsSelected(factorIds) {
        console.log("因子选择结果:", factorIds)
        selectedFactorIds = factorIds
    }
    
    // 处理对话框关闭 - 简化版本
    function handleDialogClosed() {
        console.log("因子选择对话框已关闭")
        if (factorSelectorDialog) {
            factorSelectorDialog.destroy()
            factorSelectorDialog = null
        }
    }
    
    // 移除已选择的因子 - 简化版本
    function removeSelectedFactor(factorId) {
        var index = selectedFactorIds.indexOf(factorId)
        if (index !== -1) {
            selectedFactorIds.splice(index, 1)
        }
    }
    
    // ============ 数据日期范围获取 ============
    // 日期范围由用户通过UI选择（如"最近1年"、"最近3年"等），不再自动获取
    
    // ============ 数据源相关函数 ============
    // 数据源处理已移至C++控制器，QML不再处理缓存选择逻辑
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        console.log("因子回测页面初始化完成")
        console.log("因子服务:", factorService)
        console.log("当前选择因子:", selectedFactorId)
        console.log("当前选择因子列表:", selectedFactorIds)
        factorBacktestController.dataSourceMode = selectedDataSourceMode

        if (cleanedDataController) {
            if (!cleanedDataController.isAvailable) {
                cleanedDataController.initialize()
            }
            cleanedDataController.refreshDatasets()
        }

        rebuildCacheDatasetOptions()
        if (factorBacktestController.backtestResult) {
            root.applyDisplayedBacktestResult(factorBacktestController.backtestResult)
        }
        
        // 数据源和日期范围处理已移至C++控制器，QML只负责UI显示
        console.log("因子回测页面初始化完成，等待用户操作")
    }

    Connections {
        target: cleanedDataController

        function onDatasetListChanged() {
            rebuildCacheDatasetOptions()
            syncSelectedDatasetIndex()
        }

        function onSelectedDatasetChanged() {
            if (cleanedDataController && cleanedDataController.selectedDatasetInfo && cleanedDataController.selectedDatasetInfo.id !== undefined) {
                selectedDatasetId = cleanedDataController.selectedDatasetInfo.id
                factorBacktestController.selectedDatasetId = selectedDatasetId
            }
            rebuildCacheDatasetOptions()
            syncSelectedDatasetIndex()
        }
    }
}
