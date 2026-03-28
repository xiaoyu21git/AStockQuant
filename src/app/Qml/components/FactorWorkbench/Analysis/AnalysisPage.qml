// AnalysisPage.qml
// 因子分析页面 - 模块化组件
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge

/**
 * 因子分析页面组件
 * 提供因子绩效分析功能
 */
Item {
    id: root
    
    // ============ 属性 ============
    
    property Bridge.FactorService factorService: null  // 使用新的FactorService类型
    property string selectedFactorId: ""
    
    // ============ UI ============
    
    Rectangle {
        anchors.fill: parent
        color: "#0F172A"
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            anchors.topMargin: 40  // 增加顶部边距以避免与工作流导航栏重叠
            spacing: 16
            
            // 标题
            Text {
                text: "📊 因子分析工作区"
                font.pixelSize: 20
                font.weight: Font.DemiBold
                color: "#F1F5F9"
            }
            
            // 分析看板
            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 2
                columnSpacing: 16
                rowSpacing: 16
                
                // IC分析卡片
                AnalysisCard {
                    title: "IC分析"
                    value: selectedFactorId ? getICValue(selectedFactorId) : "0.000"
                    trend: "up"
                    description: "信息系数"
                    cardColor: "#3B82F6"
                }
                
                // IR分析卡片
                AnalysisCard {
                    title: "IR分析"
                    value: selectedFactorId ? getIRValue(selectedFactorId) : "0.00"
                    trend: "neutral"
                    description: "信息比率"
                    cardColor: "#10B981"
                }
                
                // 分组收益卡片
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    radius: 12
                    color: "#1E293B"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        
                        Text {
                            text: "📊 分组收益"
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                        }
                        
                        Text {
                            text: selectedFactorId ? "单调性: " + getMonotonicity(selectedFactorId) : "选择因子查看"
                            font.pixelSize: 12
                            color: selectedFactorId ? "#10B981" : "#94A3B8"
                        }
                    }
                }
                
                // 稳定性分析卡片
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    radius: 12
                    color: "#1E293B"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        
                        Text {
                            text: "📈 稳定性分析"
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                        }
                        
                        Text {
                            text: selectedFactorId ? "滚动IC稳定" : "选择因子查看"
                            font.pixelSize: 12
                            color: selectedFactorId ? "#10B981" : "#94A3B8"
                        }
                    }
                }
                
                // 换手率分析
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    radius: 12
                    color: "#1E293B"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        
                        Text {
                            text: "🔄 换手率分析"
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                        }
                        
                        Text {
                            text: selectedFactorId ? "换手率: " + getTurnoverRate(selectedFactorId) + "%" : "选择因子查看"
                            font.pixelSize: 12
                            color: selectedFactorId ? "#F59E0B" : "#94A3B8"
                        }
                    }
                }
                
                // 有效期分析
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    radius: 12
                    color: "#1E293B"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        
                        Text {
                            text: "📅 有效期分析"
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            color: "#F1F5F9"
                        }
                        
                        Text {
                            text: selectedFactorId ? "有效期: " + getValidityDays(selectedFactorId) + "天" : "选择因子查看"
                            font.pixelSize: 12
                            color: selectedFactorId ? "#8B5CF6" : "#94A3B8"
                        }
                    }
                }
            }
            
            // 操作按钮
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                
                // 导出报告按钮
                Rectangle {
                    Layout.preferredWidth: 120
                    Layout.preferredHeight: 40
                    radius: 8
                    color: "#3B82F6"
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 8
                        
                        Text {
                            text: "📤"
                            font.pixelSize: 14
                            color: "white"
                        }
                        
                        Text {
                            text: "导出报告"
                            font.pixelSize: 14
                            color: "white"
                        }
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: exportReport()
                    }
                }
                
                // 详细分析按钮
                Rectangle {
                    Layout.preferredWidth: 120
                    Layout.preferredHeight: 40
                    radius: 8
                    color: "#334155"
                    
                    Row {
                        anchors.centerIn: parent
                        spacing: 8
                        
                        Text {
                            text: "🔍"
                            font.pixelSize: 14
                            color: "#F1F5F9"
                        }
                        
                        Text {
                            text: "详细分析"
                            font.pixelSize: 14
                            color: "#F1F5F9"
                        }
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: showDetailedAnalysis()
                    }
                }
                
                Item { Layout.fillWidth: true }
                
                // 状态提示
                Text {
                    text: selectedFactorId ? "正在分析: " + getFactorName(selectedFactorId) : "请从因子库选择因子进行分析"
                    font.pixelSize: 14
                    color: selectedFactorId ? "#3B82F6" : "#94A3B8"
                }
            }
        }
    }
    
    // ============ 组件定义 ============
    
    // 分析卡片组件
    component AnalysisCard: Item {
        property string title: ""
        property string value: ""
        property string trend: "neutral"  // up, down, neutral
        property string description: ""
        property color cardColor: "#3B82F6"
        
        Layout.fillWidth: true
        Layout.preferredHeight: 120
        
        Rectangle {
            anchors.fill: parent
            radius: 12
            color: "#1E293B"
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8
                
                Text {
                    text: title
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: "#F1F5F9"
                }
                
                Row {
                    spacing: 8
                    
                    Text {
                        text: value
                        font.pixelSize: 24
                        font.weight: Font.DemiBold
                        color: cardColor
                    }
                    
                    // 趋势指示器
                    Text {
                        visible: trend !== "neutral"
                        text: trend === "up" ? "↑" : "↓"
                        font.pixelSize: 16
                        color: trend === "up" ? "#10B981" : "#EF4444"
                    }
                }
                
                Text {
                    text: description
                    font.pixelSize: 12
                    color: "#94A3B8"
                }
            }
        }
    }
    
    // ============ 内部函数 ============
    
    // 获取因子IC值
    function getICValue(factorId) {
        return "0.000"
    }
    
    // 获取因子IR值
    function getIRValue(factorId) {
        return "0.00"
    }
    
    // 获取单调性
    function getMonotonicity(factorId) {
        return "✓"
    }
    
    // 获取换手率
    function getTurnoverRate(factorId) {
        return "25"
    }
    
    // 获取有效期
    function getValidityDays(factorId) {
        return "30"
    }
    
    // 获取因子名称
    function getFactorName(factorId) {
        return "示例因子"
    }
    
    // 导出报告
    function exportReport() {
        console.log("导出分析报告")
    }
    
    // 显示详细分析
    function showDetailedAnalysis() {
        console.log("显示详细分析")
    }
    
    // 显示提示消息
    function showToast(message) {
        console.log("提示:", message)
    }
}