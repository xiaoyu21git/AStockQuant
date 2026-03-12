// FactorDetailDialog.qml
// 因子详情对话框组件
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

Dialog {
    id: root
    
    // ============ 公共属性 ============
    
    property string factorId: ""
    property string factorName: ""
    property string displayName: ""
    property string majorCategory: ""
    property string subCategory: ""
    property string description: ""
    property real icValue: 0.0
    property real irValue: 0.0
    property int validityDays: 0
    property real turnoverRate: 0.0
    property bool isFavorite: false
    property string status: ""
    property var tags: []
    property string creator: ""
    property string createDate: ""
    
    // ============ 对话框属性 ============
    
    title: "因子详情: " + (displayName || factorName)
    width: 800  // 适当增加宽度，以便内容更清晰
    height: 700  // 适当增加高度，以便内容更清晰
    x: (parent ? (parent.width - width) / 2 : 0)  // 居中显示
    y: (parent ? (parent.height - height) / 2 : 0)  // 居中显示
    modal: true
    dim: true
    padding: 0
    background: Rectangle {
        color: "#0F172A"  // 添加背景颜色，与主界面一致
        radius: 12
        border.width: 1
        border.color: "#1E293B"
    }
    
    // ============ 主布局 ============
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // 标题栏
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: "#1E293B"  // bgSecondary
            
            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                
                Text {
                    text: "📊"
                    font.pixelSize: 20
                    color: "#F1F5F9"
                }
                
                Text {
                    text: root.title
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: "#F1F5F9"  // textPrimary
                    elide: Text.ElideRight
                }
                
                Item { Layout.fillWidth: true }
                
                // 收藏按钮
                Text {
                    text: isFavorite ? "⭐" : "☆"
                    font.pixelSize: 20
                    color: isFavorite ? "#F59E0B" : "#94A3B8"
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: isFavorite = !isFavorite
                    }
                }
                
                // 关闭按钮
                Text {
                    text: "✕"
                    font.pixelSize: 18
                    color: "#94A3B8"
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                    }
                }
            }
        }
        
        // 内容区域
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            ColumnLayout {
                width: parent.width
                spacing: 16
                //padding: 16
                
                // 基本信息卡片
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120  // 减小高度
                    radius: 8
                    color: "#1E293B"  // bgSecondary
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12  // 减小边距
                        spacing: 6  // 减小间距
                        
                        RowLayout {
                            spacing: 12
                            
                            Text {
                                text: displayName
                                font.pixelSize: 20
                                font.weight: Font.Bold
                                color: "#F1F5F9"
                            }
                            
                            Rectangle {
                                width: 80
                                height: 24
                                radius: 4
                                color: majorCategory === "动量类" ? "#3B82F6" :
                                       majorCategory === "价值类" ? "#10B981" :
                                       majorCategory === "质量类" ? "#F59E0B" :
                                       majorCategory === "成长类" ? "#8B5CF6" : "#64748B"
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: majorCategory
                                    font.pixelSize: 12
                                    color: "white"
                                }
                            }
                            
                            Rectangle {
                                width: 100
                                height: 24
                                radius: 4
                                color: "#334155"
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: subCategory
                                    font.pixelSize: 12
                                    color: "#F1F5F9"
                                }
                            }
                        }
                        
                        Text {
                            text: description
                            font.pixelSize: 14
                            color: "#94A3B8"
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        
                        RowLayout {
                            spacing: 12
                            
                            Text {
                                text: "创建者: " + creator
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            Text {
                                text: "创建时间: " + createDate
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            Item { Layout.fillWidth: true }
                            
                            Rectangle {
                                width: 80
                                height: 24
                                radius: 4
                                color: status === "ACTIVE" ? "#10B981" : "#EF4444"
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: status === "ACTIVE" ? "已生效" : "已停用"
                                    font.pixelSize: 12
                                    color: "white"
                                }
                            }
                        }
                    }
                }
                
                // 性能指标卡片
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 100  // 减小高度
                    radius: 8
                    color: "#1E293B"
                    
                    GridLayout {
                        anchors.fill: parent
                        anchors.margins: 12  // 减小边距
                        columns: 3
                        columnSpacing: 12  // 减小间距
                        rowSpacing: 6  // 减小间距
                        
                        // IC指标
                        Column {
                            spacing: 4
                            
                            Text {
                                text: "IC值"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            Row {
                                spacing: 6
                                
                                Text {
                                    text: icValue.toFixed(3)
                                    font.pixelSize: 24
                                    font.weight: Font.Bold
                                    color: icValue > 0.03 ? "#10B981" : 
                                           icValue > 0.01 ? "#F59E0B" : "#EF4444"
                                }
                                
                                Text {
                                    text: icValue > 0.03 ? "📈" : "📉"
                                    font.pixelSize: 16
                                }
                            }
                            
                            Text {
                                text: icValue > 0.03 ? "表现优秀" :
                                      icValue > 0.01 ? "表现一般" : "表现较差"
                                font.pixelSize: 12
                                color: icValue > 0.03 ? "#10B981" : 
                                       icValue > 0.01 ? "#F59E0B" : "#EF4444"
                            }
                        }
                        
                        // IR指标
                        Column {
                            spacing: 4
                            
                            Text {
                                text: "IR值"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            Row {
                                spacing: 6
                                
                                Text {
                                    text: irValue.toFixed(2)
                                    font.pixelSize: 24
                                    font.weight: Font.Bold
                                    color: irValue > 2.0 ? "#10B981" : 
                                           irValue > 1.5 ? "#F59E0B" : "#EF4444"
                                }
                                
                                Text {
                                    text: irValue > 2.0 ? "⭐" : "📊"
                                    font.pixelSize: 16
                                }
                            }
                            
                            Text {
                                text: irValue > 2.0 ? "风险调整收益高" :
                                      irValue > 1.5 ? "风险调整收益中等" : "风险调整收益低"
                                font.pixelSize: 12
                                color: irValue > 2.0 ? "#10B981" : 
                                       irValue > 1.5 ? "#F59E0B" : "#EF4444"
                            }
                        }
                        
                        // 换手率
                        Column {
                            spacing: 4
                            
                            Text {
                                text: "换手率"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            Row {
                                spacing: 6
                                
                                Text {
                                    text: turnoverRate.toFixed(1) + "%"
                                    font.pixelSize: 24
                                    font.weight: Font.Bold
                                    color: turnoverRate < 30 ? "#10B981" : 
                                           turnoverRate < 50 ? "#F59E0B" : "#EF4444"
                                }
                                
                                Text {
                                    text: turnoverRate < 30 ? "⚡" : "⏳"
                                    font.pixelSize: 16
                                }
                            }
                            
                            Text {
                                text: turnoverRate < 30 ? "换手较低" :
                                      turnoverRate < 50 ? "换手中等" : "换手较高"
                                font.pixelSize: 12
                                color: turnoverRate < 30 ? "#10B981" : 
                                       turnoverRate < 50 ? "#F59E0B" : "#EF4444"
                            }
                        }
                        
                        // 有效期
                        Column {
                            spacing: 4
                            
                            Text {
                                text: "有效期(天)"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            Text {
                                text: validityDays
                                font.pixelSize: 20
                                font.weight: Font.Bold
                                color: "#F1F5F9"
                            }
                            
                            Text {
                                text: validityDays > 90 ? "长期有效" :
                                      validityDays > 30 ? "中期有效" : "短期有效"
                                font.pixelSize: 12
                                color: validityDays > 90 ? "#10B981" : 
                                       validityDays > 30 ? "#F59E0B" : "#EF4444"
                            }
                        }
                        
                        // 因子ID
                        Column {
                            spacing: 4
                            
                            Text {
                                text: "因子ID"
                                font.pixelSize: 12
                                color: "#94A3B8"
                            }
                            
                            Text {
                                text: factorId
                                font.pixelSize: 14
                                color: "#F1F5F9"
                                font.family: "Monospace"
                            }
                        }
                    }
                }
                
                // 标签区域
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 70  // 减小高度
                    radius: 8
                    color: "#1E293B"
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12  // 减小边距
                        spacing: 6  // 减小间距
                        
                        Text {
                            text: "标签"
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: "#F1F5F9"
                        }
                        
                        Flow {
                            Layout.fillWidth: true
                            spacing: 8
                            
                            Repeater {
                                model: tags
                                
                                Rectangle {
                                    height: 24
                                    radius: 12
                                    color: "#334155"
                                    
                                    Text {
                                        anchors.centerIn: parent
                                        anchors.margins: 8
                                        text: modelData
                                        font.pixelSize: 12
                                        color: "#F1F5F9"
                                    }
                                }
                            }
                        }
                    }
                }
                
                // 操作按钮区域
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60
                    spacing: 12
                    
                    // 分析按钮
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        radius: 8
                        color: "#3B82F6"
                        
                        Row {
                            anchors.centerIn: parent
                            spacing: 8
                            
                            Text {
                                text: "📈"
                                font.pixelSize: 16
                                color: "white"
                            }
                            
                            Text {
                                text: "详细分析"
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: "white"
                            }
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                console.log("分析因子:", factorId)
                                // 可以触发分析对话框
                            }
                        }
                    }
                    
                    // 加入组合按钮
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        radius: 8
                        color: "#10B981"
                        
                        Row {
                            anchors.centerIn: parent
                            spacing: 8
                            
                            Text {
                                text: "➕"
                                font.pixelSize: 16
                                color: "white"
                            }
                            
                            Text {
                                text: "加入组合"
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: "white"
                            }
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                console.log("加入组合:", factorId)
                            }
                        }
                    }
                    
                    // 编辑按钮
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        radius: 8
                        color: "#334155"
                        
                        Row {
                            anchors.centerIn: parent
                            spacing: 8
                            
                            Text {
                                text: "✏️"
                                font.pixelSize: 16
                                color: "#F1F5F9"
                            }
                            
                            Text {
                                text: "编辑因子"
                                font.pixelSize: 14
                                color: "#F1F5F9"
                            }
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                console.log("编辑因子:", factorId)
                            }
                        }
                    }
                }
            }
        }
    }
    
    // ============ 公共方法 ============
    
    function openWithFactor(factorId) {
        console.log("打开因子详情对话框，因子ID:", factorId)
        
        // 模拟加载因子数据
        // 实际应用中应该从数据源加载
        if (factorId === "momentum_20d") {
            root.factorId = "momentum_20d"
            root.factorName = "momentum_20d"
            root.displayName = "MA_20"
            root.majorCategory = "动量类"
            root.subCategory = "趋势动量"
            root.description = "20日移动平均线，经典的动量因子"
            root.icValue = 0.042
            root.irValue = 2.33
            root.validityDays = 20
            root.turnoverRate = 32
            root.isFavorite = true
            root.status = "ACTIVE"
            root.tags = ["动量", "趋势", "技术指标"]
            root.creator = "张三"
            root.createDate = "2024-01"
        } else {
            // 默认值
            root.factorId = factorId
            root.displayName = "未知因子"
            root.description = "因子详情加载中..."
            root.icValue = 0.0
            root.irValue = 0.0
            root.turnoverRate = 0.0
            root.status = "UNKNOWN"
        }
        
        root.open()
    }
    
    // ============ 信号处理 ============
    
    onOpened: {
        console.log("因子详情对话框已打开:", factorId)
    }
    
    onClosed: {
        console.log("因子详情对话框已关闭")
    }
    
    // ============ 初始化 ============
    
    Component.onCompleted: {
        console.log("因子详情对话框组件初始化")
    }
}