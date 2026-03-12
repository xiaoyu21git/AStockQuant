// FactorPerformanceChart.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15

/**
 * 因子性能图表组件
 * 显示因子IC走势、IR值等性能图表
 */
Rectangle {
    id: root
    
    // ============ 公共属性 ============
    
    property string factorName: ""
    property string displayName: ""
    property var performanceData: []  // [{date: "2023-01-01", ic: 0.042, ir: 1.24}, ...]
    
    // 图表类型
    property string chartType: "ic"  // "ic", "ir", "cumulative"
    
    // 时间范围
    property string timeRange: "1y"  // "1m", "3m", "6m", "1y", "all"
    
    // 信号
    signal chartTypeChanged(string newType)
    signal timeRangeChanged(string newRange)
    
    // ============ 视觉属性 ============
    
    implicitWidth: 400
    implicitHeight: 300
    radius: 16  // borderRadiusXl
    color: "#1E293B"  // bgSecondary
    
    // ============ 主布局 ============
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16  // spacing4
        spacing: 12  // spacing3
        
        // 图表标题和控件
        RowLayout {
            spacing: 12  // spacing3
            
            Text {
                text: displayName || factorName
                font.pixelSize: 16  // fontSizeLg
                font.weight: Font.DemiBold
                color: "#F1F5F9"  // textPrimary
                elide: Text.ElideRight
            }
            
            Item {
                Layout.fillWidth: true
            }
            
            // 图表类型选择
            Row {
                spacing: 4  // spacing1
                
                // IC图表
                Rectangle {
                    width: 60
                    height: 28
                    radius: 8  // borderRadiusMd
                    color: chartType === "ic" ? "#3B82F6"  // factorMomentum
                                              : "#334155"  // bgTertiary
                    
                    Text {
                        anchors.centerIn: parent
                        text: "IC"
                        font.pixelSize: 12  // fontSizeSm
                        color: chartType === "ic" ? "white" : "#94A3B8"  // textSecondary
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            chartType = "ic"
                            chartTypeChanged("ic")
                        }
                    }
                }
                
                // IR图表
                Rectangle {
                    width: 60
                    height: 28
                    radius: 8  // borderRadiusMd
                    color: chartType === "ir" ? "#3B82F6"  // factorMomentum
                                              : "#334155"  // bgTertiary
                    
                    Text {
                        anchors.centerIn: parent
                        text: "IR"
                        font.pixelSize: 12  // fontSizeSm
                        color: chartType === "ir" ? "white" : "#94A3B8"  // textSecondary
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            chartType = "ir"
                            chartTypeChanged("ir")
                        }
                    }
                }
                
                // 累计收益
                Rectangle {
                    width: 80
                    height: 28
                    radius: 8  // borderRadiusMd
                    color: chartType === "cumulative" ? "#3B82F6"  // factorMomentum
                                                      : "#334155"  // bgTertiary
                    
                    Text {
                        anchors.centerIn: parent
                        text: "累计收益"
                        font.pixelSize: 12  // fontSizeSm
                        color: chartType === "cumulative" ? "white" : "#94A3B8"  // textSecondary
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            chartType = "cumulative"
                            chartTypeChanged("cumulative")
                        }
                    }
                }
            }
        }
        
        // 时间范围选择
        Row {
            spacing: 4  // spacing1
            
            // 1个月
            Rectangle {
                width: 50
                height: 24
                radius: 4  // borderRadiusSm
                color: timeRange === "1m" ? "#3B82F6"  // factorMomentum
                                          : "#334155"  // bgTertiary
                
                Text {
                    anchors.centerIn: parent
                    text: "1M"
                    font.pixelSize: 10  // fontSizeXs
                    color: timeRange === "1m" ? "white" : "#94A3B8"  // textSecondary
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        timeRange = "1m"
                        timeRangeChanged("1m")
                    }
                }
            }
            
            // 3个月
            Rectangle {
                width: 50
                height: 24
                radius: 4  // borderRadiusSm
                color: timeRange === "3m" ? "#3B82F6"  // factorMomentum
                                          : "#334155"  // bgTertiary
                
                Text {
                    anchors.centerIn: parent
                    text: "3M"
                    font.pixelSize: 10  // fontSizeXs
                    color: timeRange === "3m" ? "white" : "#94A3B8"  // textSecondary
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        timeRange = "3m"
                        timeRangeChanged("3m")
                    }
                }
            }
            
            // 6个月
            Rectangle {
                width: 50
                height: 24
                radius: 4  // borderRadiusSm
                color: timeRange === "6m" ? "#3B82F6"  // factorMomentum
                                          : "#334155"  // bgTertiary
                
                Text {
                    anchors.centerIn: parent
                    text: "6M"
                    font.pixelSize: 10  // fontSizeXs
                    color: timeRange === "6m" ? "white" : "#94A3B8"  // textSecondary
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        timeRange = "6m"
                        timeRangeChanged("6m")
                    }
                }
            }
            
            // 1年
            Rectangle {
                width: 50
                height: 24
                radius: 4  // borderRadiusSm
                color: timeRange === "1y" ? "#3B82F6"  // factorMomentum
                                          : "#334155"  // bgTertiary
                
                Text {
                    anchors.centerIn: parent
                    text: "1Y"
                    font.pixelSize: 10  // fontSizeXs
                    color: timeRange === "1y" ? "white" : "#94A3B8"  // textSecondary
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        timeRange = "1y"
                        timeRangeChanged("1y")
                    }
                }
            }
            
            // 全部
            Rectangle {
                width: 50
                height: 24
                radius: 4  // borderRadiusSm
                color: timeRange === "all" ? "#3B82F6"  // factorMomentum
                                           : "#334155"  // bgTertiary
                
                Text {
                    anchors.centerIn: parent
                    text: "全部"
                    font.pixelSize: 10  // fontSizeXs
                    color: timeRange === "all" ? "white" : "#94A3B8"  // textSecondary
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        timeRange = "all"
                        timeRangeChanged("all")
                    }
                }
            }
        }
        
        // 图表区域
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 12  // borderRadiusLg
            color: "#0F172A"  // bgPrimary
            
            // 模拟图表 - 实际应该使用QtCharts
            Column {
                anchors.centerIn: parent
                spacing: 8  // spacing2
                
                // 图表占位
                Rectangle {
                    width: 300
                    height: 180
                    radius: 8  // borderRadiusMd
                    color: "#1E293B"  // bgSecondary
                    
                    Text {
                        anchors.centerIn: parent
                        text: "📈 性能图表\n（开发中）"
                        font.pixelSize: 14  // fontSizeMd
                        color: "#94A3B8"  // textSecondary
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
                
                // 统计数据
                Row {
                    spacing: 16  // spacing4
                    
                    StatisticBadge {
                        label: "平均IC"
                        value: "+0.042"
                        color: "#4caf50"  // statusSuccess
                    }
                    
                    StatisticBadge {
                        label: "IR值"
                        value: "1.24"
                        color: "#4caf50"  // statusSuccess
                    }
                    
                    StatisticBadge {
                        label: "胜率"
                        value: "68.5%"
                        color: "#4caf50"  // statusSuccess
                    }
                    
                    StatisticBadge {
                        label: "最大回撤"
                        value: "-8.3%"
                        color: "#f44336"  // statusError
                    }
                }
            }
        }
        
        // 图例
        Row {
            Layout.fillWidth: true
            spacing: 12  // spacing3
            
            // IC图例
            LegendItem {
                color: "#3B82F6"  // factorMomentum
                label: "IC值"
            }
            
            // 基准线图例
            LegendItem {
                color: "#64748B"  // textTertiary
                label: "基准线(0)"
                lineStyle: true
            }
            
            // 移动平均图例
            LegendItem {
                color: "#4caf50"  // statusSuccess
                label: "20日移动平均"
                lineStyle: true
                dashPattern: [5, 5]
            }
            
            Item {
                Layout.fillWidth: true
            }
            
            // 导出按钮
            Rectangle {
                width: 80
                height: 28
                radius: 8  // borderRadiusMd
                color: "#334155"  // bgTertiary
                
                Row {
                    anchors.centerIn: parent
                    spacing: 4  // spacing1
                    
                    Text {
                        text: "📥"
                        font.pixelSize: 12  // fontSizeSm
                        color: "#94A3B8"  // textSecondary
                    }
                    
                    Text {
                        text: "导出"
                        font.pixelSize: 12  // fontSizeSm
                        color: "#94A3B8"  // textSecondary
                    }
                }
                
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: console.log("导出图表数据")
                }
            }
        }
    }
    
    // ============ 子组件 ============
    
    /**
     * 统计数字徽章
     */
    component StatisticBadge: Column {
        property string label: ""
        property string value: ""
        property color color: "#94A3B8"  // textSecondary
        
        spacing: 0
        
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: label
            font.pixelSize: 10  // fontSizeXs
            color: "#64748B"  // textTertiary
        }
        
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: value
            font.pixelSize: 16  // fontSizeLg
            font.weight: Font.Bold
            color: parent.color
        }
    }
    
    /**
     * 图例项
     */
    component LegendItem: Row {
        property color color: "gray"
        property string label: ""
        property bool lineStyle: false
        property var dashPattern: []
        
        spacing: 4  // spacing1
        
        // 颜色标识
        Rectangle {
            width: 16
            height: 2
            radius: 1
            color: parent.color
            anchors.verticalCenter: parent.verticalCenter
            
            Canvas {
                anchors.fill: parent
                visible: dashPattern.length > 0
                
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    
                    ctx.strokeStyle = parent.color
                    ctx.lineWidth = 2
                    ctx.setLineDash(dashPattern)
                    ctx.beginPath()
                    ctx.moveTo(0, height/2)
                    ctx.lineTo(width, height/2)
                    ctx.stroke()
                }
            }
        }
        
        // 标签
        Text {
            text: label
            font.pixelSize: 10  // fontSizeXs
            color: "#94A3B8"  // textSecondary
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    
    // ============ 业务逻辑函数 ============
    
    // 更新性能数据
    function updatePerformanceData(data) {
        performanceData = data
        // TODO: 更新图表显示
    }
    
    // 重置图表
    function resetChart() {
        chartType = "ic"
        timeRange = "1y"
    }
}