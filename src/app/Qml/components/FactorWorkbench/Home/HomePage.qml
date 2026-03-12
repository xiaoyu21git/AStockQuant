// HomePage.qml
// 首页模块 - FactorWorkbench的首页
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import AStock.Bridge 1.0 as Bridge

/**
 * 首页模块组件
 * 提供欢迎界面和功能导航
 */
Item {
    id: root
    
    // ============ 信号 ============
    
    signal startCreation()
    signal openLibrary()
    signal openDebug()
    signal openAnalysis()
    signal openBacktest()
    
    // ============ 属性 ============
    
    property Bridge.GlobalDataService globalDataService: null
    
    // ============ UI ============
    
    Rectangle {
        anchors.fill: parent
        color: "#0F172A"
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            anchors.topMargin: 40  // 增加顶部边距以避免与工作流导航栏重叠
            spacing: 20
            
            // 欢迎区域
            ColumnLayout {
                spacing: 8
                
                Text {
                    text: "🎯 欢迎使用因子分析工作台"
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                    color: "#F1F5F9"
                }
                
                Text {
                    text: "一站式因子创建、调试、分析和回测平台"
                    font.pixelSize: 15
                    color: "#E2E8F0"
                }
                
                // 操作按钮行
                Row {
                    spacing: 16
                    topPadding: 20
                    bottomPadding: 16
                    
                    // 新建因子按钮
                    Rectangle {
                        width: 120
                        height: 40
                        radius: 8
                        color: "#3B82F6"
                        
                        Row {
                            anchors.centerIn: parent
                            spacing: 8
                            
                            Text {
                                text: "➕"
                                font.pixelSize: 16
                                color: "white"
                            }
                            
                            Text {
                                text: "新建因子"
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: "white"
                            }
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.startCreation()
                        }
                    }
                    
                    // 导入按钮
                    Rectangle {
                        width: 100
                        height: 40
                        radius: 8
                        color: "#334155"
                        
                        Row {
                            anchors.centerIn: parent
                            spacing: 8
                            
                            Text {
                                text: "📥"
                                font.pixelSize: 16
                                color: "#F1F5F9"
                            }
                            
                            Text {
                                text: "导入"
                                font.pixelSize: 14
                                color: "#F1F5F9"
                            }
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: importFactors()
                        }
                    }
                }
            }
            
            // 功能卡片
            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 12
                
                // 因子库卡片
                FeatureCard {
                    title: "📚 因子库"
                    description: "浏览、筛选和管理所有因子"
                    cardColor: "#3B82F6"
                    icon: "📚"
                    onClicked: root.openLibrary()
                }
                
                // 创建因子卡片
                FeatureCard {
                    title: "📝 创建因子"
                    description: "使用模板或自定义创建量化因子"
                    cardColor: "#10B981"
                    icon: "➕"
                    onClicked: root.startCreation()
                }
                
                // 调试因子卡片
                FeatureCard {
                    title: "🔧 调试因子"
                    description: "实时调整参数并预览效果"
                    cardColor: "#F59E0B"
                    icon: "⚙️"
                    onClicked: root.openDebug()
                }
                
                // 分析因子卡片
                FeatureCard {
                    title: "📊 分析因子"
                    description: "全面的因子绩效和统计指标分析"
                    cardColor: "#8B5CF6"
                    icon: "📈"
                    onClicked: root.openAnalysis()
                }
                
                // 回测试验卡片
                FeatureCard {
                    title: "🧪 回测试验"
                    description: "基于历史数据的因子表现测试"
                    cardColor: "#EC4899"
                    icon: "🔬"
                    onClicked: root.openBacktest()
                }
            }
        }
    }
    
    // ============ 内部函数 ============
    
    function importFactors() {
        console.log("导入因子功能")
        // TODO: 实现导入功能
    }
    
    // ============ 组件定义 ============
    
    // 功能卡片组件 - 缩小高度版本
    component FeatureCard: Item {
        property string title: ""
        property string description: ""
        property color cardColor: "#3B82F6"
        property string icon: "📊"
        signal clicked()
        
        Layout.fillWidth: true
        Layout.fillHeight: true
        
        Rectangle {
            id: background
            anchors.fill: parent
            radius: 12
            color: "#1E293B"
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12  // 减少边距
                spacing: 6  // 减少间距
                
                Row {
                    spacing: 10  // 减少间距
                    
                    // 图标 - 缩小
                    Rectangle {
                        width: 32  // 缩小
                        height: 32  // 缩小
                        radius: 6  // 缩小
                        color: Qt.rgba(cardColor.r, cardColor.g, cardColor.b, 0.2)
                        
                        Text {
                            anchors.centerIn: parent
                            text: icon
                            font.pixelSize: 14  // 缩小
                        }
                    }
                    
                    // 标题
                    Text {
                        text: title
                        font.pixelSize: 14  // 缩小
                        font.weight: Font.DemiBold
                        color: "#F1F5F9"
                    }
                }
                
                // 描述
                Text {
                    Layout.fillWidth: true
                    text: description
                    font.pixelSize: 12  // 缩小
                    color: "#94A3B8"
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2  // 限制行数
                    elide: Text.ElideRight
                }
                
                Item { Layout.fillHeight: true }
                
                // 操作按钮 - 缩小
                Rectangle {
                    width: 70  // 缩小
                    height: 28  // 缩小
                    radius: 6
                    color: cardColor
                    
                    Text {
                        anchors.centerIn: parent
                        text: "开始"
                        font.pixelSize: 11  // 缩小
                        color: "white"
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: clicked()
                    }
                }
            }
        }
    }
}