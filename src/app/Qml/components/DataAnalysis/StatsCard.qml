// StatsCard.qml - 统计卡片组件（修正版）
import QtQuick 2.15
import QtQuick.Controls 2.15
import ConsoleUi 1.0 as ConsoleTheme

Rectangle {
    id: statsCard
    width: 250
    height: 150
    radius: 10
    color: ConsoleTheme.darkCard  // 确保ConsoleTheme有这个属性
    border.color: ConsoleTheme.darkBorder
    border.width: 1
    
    property string iconSource: "qrc:/icons/filter.svg"
    property color iconColor: ConsoleTheme.primaryColor  // 改为color类型
    property string label: "统计标签"
    property string value: "0"
    property string trendText: "趋势文本"
    property bool trendUp: true
    
    // 鼠标悬停效果
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        
        onEntered: {
            statsCard.scale = 1.05
            statsCard.z = 1
        }
        onExited: {
            statsCard.scale = 1.0
            statsCard.z = 0
        }
    }
    
    // 计算带透明度的颜色
    function getColorWithOpacity(baseColor, opacityPercent) {
        var color = Qt.color(baseColor)
        color.a = opacityPercent / 100.0
        return color
    }
    
    // 内容布局
    Column {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12
        
        // 头部
        Row {
            width: parent.width
            spacing: 10
            
            Text {
                text: statsCard.label
                font.pixelSize: 14
                color: ConsoleTheme.darkText ? ConsoleTheme.darkText : "#bebaba"  // 备用值
                anchors.verticalCenter: parent.verticalCenter
            }
            
            // 图标
            Rectangle {
                width: 50
                height: 50
                radius: 10
                color: getColorWithOpacity(statsCard.iconColor, 20)  // 20%透明度
                anchors.verticalCenter: parent.verticalCenter
                
                Image {
                    source: statsCard.iconSource
                    width: 24
                    height: 24
                    anchors.centerIn: parent
                    //color: statsCard.iconColor
                }
            }
        }
        
        // 数值
        Text {
            text: statsCard.value
            font.pixelSize: 44
            font.bold: true
            color: ConsoleTheme.darkText ? ConsoleTheme.darkText : "#e0e0e0"
        }
        
        // 趋势
        Row {
            spacing: 5
            
            Image {
                source: statsCard.trendUp ? "qrc:/icons/arrow-up.svg" : "qrc:/icons/arrow-down.svg"
                width: 16
                height: 16
                // color: statsCard.trendUp ? 
                //        (ConsoleTheme.successColor ? ConsoleTheme.successColor : "#4caf50") : 
                //        (ConsoleTheme.dangerColor ? ConsoleTheme.dangerColor : "#f44336")
                anchors.verticalCenter: parent.verticalCenter
            }
            
            Text {
                text: statsCard.trendText
                font.pixelSize: 14
                font.weight: Font.Medium
                color: statsCard.trendUp ? 
                       (ConsoleTheme.successColor ? ConsoleTheme.successColor : "#4caf50") : 
                       (ConsoleTheme.dangerColor ? ConsoleTheme.dangerColor : "#f44336")
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}